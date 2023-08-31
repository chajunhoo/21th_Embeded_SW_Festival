
let APP_ID="9c86bdd8e922402895b2fe8e0e9f753e"

let token = null;
let uid = String(Math.floor(Math.random() * 10000))

let client;
let channel;

let localStream;
let remoteStream;
let peerConnection;

let dataChannel;


let message = document.getElementById('receive');

const servers = {
    iceServers:[
        {
            urls:['stun:stun1.l.google.com:19302', 'stun:stun2.l.google.com:19302']
        }
    ] 
}

let init = async() =>{
    client = await AgoraRTM.createInstance(APP_ID)
    await client.login({uid, token})

    channel = client.createChannel('main')
    await channel.join()

    channel.on('MemberJoined', handleUserJoined)
    channel.on('MemberLeft', handleUserLeft)

    client.on('MessageFromPeer', handleMessageFromPeer)

    localStream = await navigator.mediaDevices.getUserMedia({
        video:true, audio:true
    })
    document.getElementById('user-1').srcObject = localStream
}

let handleUserLeft = (MemberId)=>{ // 피어 떠났을때
    document.getElementById('user-2').style.display = 'none'
}

let handleMessageFromPeer = async(message, MemberId) =>{
    message = JSON.parse(message.text)
    if(message.type ==='offer'){
        createAnswer(MemberId, message.offer)
    }
    if(message.type ==='answer'){
        addAnswer(message.answer)
    }
    if(message.type ==='candidate'){
        if(peerConnection && peerConnection.remoteDescription){ // sdp(offer, answer) 이 NULL인가 확인
            peerConnection.addIceCandidate(message.candidate)
        }
    }
}


let handleUserJoined = async(MemberId)=>{
    console.log('A new user joined the channel:', MemberId)
    createOffer(MemberId)
}

let createPeerConnection = async (MemberId) =>{
    peerConnection = new RTCPeerConnection(servers)
    dataChannel = peerConnection.createDataChannel("data_Channel")
    
    dataChannel.onopen

    remoteStream = new MediaStream()
    document.getElementById('user-2').srcObject = remoteStream
    document.getElementById('user-2').style.display = 'block'


    if(!localStream){
        localStream = await navigator.mediaDevices.getUserMedia({
            video:true, audio:true
        })
        document.getElementById('user-1').srcObject = localStream
    }

    localStream.getTracks().forEach((track)=> {
        peerConnection.addTrack(track, localStream)
    })

    peerConnection.ontrack = (event)=>{
        event.streams[0].getTracks().forEach((track)=>{
            remoteStream.addTrack(track)
        })
    }

    peerConnection.onicecandidate = async (event)=>{ //ICEcandidate수집될때 마다 전송시킴
        if(event.candidate){
            client.sendMessageToPeer({text:JSON.stringify({'type':'candidate', 'candidate':event.candidate})},MemberId)
        }
    }

peerConnection.ondatachannel = (event) =>{
    const channel = event.channel;
    console.log('Got data channel', channel);
    console.log("datachannel open: ", event.data);

    channel.onmessage = (message)=> {
        let diagnosis = message.data;
        console.log('Got message', message.data)
        message.value = diagnosis; // textarea에 써주기 위해서는 변수로 내용의 형식을 지정해야 함
        document.getElementById('receive').value = diagnosis;
        save.addEventListener('click', saveAsFile)
    }
}
}

let createOffer = async (MemberId) => {
    await createPeerConnection(MemberId)

    let offer = await peerConnection.createOffer()
    await peerConnection.setLocalDescription(offer) //호출시 ICECandidate 수집, await 쓰면 setLocalDescription이후에 offer/answer 전송

    client.sendMessageToPeer({text:JSON.stringify({'type':'offer', 'offer':offer})},MemberId)
}

let createAnswer = async (MemberId, offer)=>{
    await createPeerConnection(MemberId)

    await peerConnection.setRemoteDescription(offer)

    let answer = await peerConnection.createAnswer()
    await peerConnection.setLocalDescription(answer)

    client.sendMessageToPeer({text:JSON.stringify({'type':'answer', 'answer':peerConnection.localDescription})},MemberId)//offer를 수신해야 answer를 생성하고 전송
}

let addAnswer = async (answer) => {
    if(!peerConnection.currentRemoteDescription){
        await peerConnection.setRemoteDescription(answer)
    }
}

let leaveChannel = async()=> {
    await channel.leave()
    await client.logout()
}

window.addEventListener('beforeunload', leaveChannel)

function toggleFullscreen(){ //전체화면
  var doc = window.document;
  var docEl = doc.documentElement; //<html>

  var requestFullscreen = docEl.requestFullscreen || docEl.mozRequestFullScreen || docEl.webkitRequestFullScreen || docEl.msRequestFullscreen;
  var exitFullscreen = doc.exitFullscreen || doc.mozCancelFullScreen || doc.webkitExitFullscreen || doc.msExitFullscreen;

  if (!doc.fullscreenElement && !doc.mozFullScreenElement && !doc.webkitFullscreenElement && !doc.msFullscreenElement) {
      requestFullscreen.call(docEl);
  } else {
      exitFullscreen.call(doc);
  }
}

function saveAsFile() { //textarea의 내용 저장
    var contents = document.getElementById('receive').value;
    let currentDate = new Date()
    var fileName = currentDate.toISOString().slice(0, 10) + '환자 이름' + '_진료기록.txt'; //currentDate : 오늘날짜, toIOString: ISO8601 형식문자열로 변환, slice(0,10): 날짜 부분만 추출

    let blob = new Blob([contents], {type: 'text/plain'});

    let link = document.createElement('a'); //html a태그 동적 생성(a태그 : 하이퍼 링크 걸어줌)
    link.href = window.URL.createObjectURL(blob);
    link.download = fileName;
    
    link.click();
}
//################################################## 시리얼 통신
let start= document.getElementById('start');
let portFlg = false;
let Port
let reader;
start.addEventListener('click', async()=>{
    try{
        if(portFlg === true){
            reader.releaseLock();
            Port.close();
        }

        Port = await navigator.serial.requestPort();
        await Port.open({baudRate: 115200});
        
        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = Port.readable.pipeTo(textDecoder.writable); 
        reader = textDecoder.readable.getReader();
        
        readData();
    }catch (error){
        console.error('시리얼 통신 시작에러', error);
    }
});

let data;
let filteredECG=0.0;
let receivedData = ""; //한줄의 길이: 18, 인덱스 0~17
let ecg=[]

async function readData(){
    try{
        portFlg = true;
        dataChannel.send("start!")
        while(portFlg){
            let{value, done} = await reader.read();
            if (done){
                alert("측정 종료");
                break;
            }
            
            receivedData += value;
            
            if(receivedData.endsWith("\n")){
                let lines = receivedData.split("\r\n"); //lines 는 배열
                for(let line of lines) {
                    if (line.trim() !== "") {
                        if(line.startsWith("DATA:")){
                            data = line.trim().split(',');
                            dataChannel.send(data);
                            receivedData="";
                        }

                        else if (line.startsWith("ECG:")){
                            dataChannel.send(line);
                            receivedData = "";
                        }
                    }
                }
            }    
        }
    }catch(error){
        console.error('데이터 수신 에러');
    }
}

const close = document.getElementById('closeButton');
close.addEventListener('click', ()=>{
    window.close();
})

//#######################################
const stop = document.getElementById('stopSerial');

stop.addEventListener('click', () => {
    reader.cancel();
    dataChannel.send('stop measurement');
})

fullscreen.addEventListener('click', init)
  fullscreen.addEventListener('click', toggleFullscreen)
fullscreen.addEventListener('click', ()=>{
    videos.classList.toggle('visible')
    fullscreen.classList.toggle('hidden')
    discription.classList.toggle('visible')
    start.classList.toggle('visible') 
    stop.classList.toggle('visible')
    close.classList.toggle('visible')
    
})
