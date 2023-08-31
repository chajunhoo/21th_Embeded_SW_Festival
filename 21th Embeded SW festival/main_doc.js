let APP_ID="9c86bdd8e922402895b2fe8e0e9f753e"

let token = null;
let uid = String(Math.floor(Math.random() * 10000))

let client;
let channel;

let localStream;
let remoteStream;
let peerConnection;

let dataChannel;

let movement=[]; //가속도
let sp02=[]; //산포도
let heartRate=[];
let ecgHeartRateMean=[];//chart2
let ecg = [];
let arrayFlg =true;

let sendBtn = document.getElementById('send')

let dataFlag = false;

let guest = false;
let measurementResult 

const layout = {
  xaxis: {
    title: 'Time',
    autorange: false, // x축 자동 범위 조정
    type: 'linear',
    showgrid: true,
    range: [0, 750],
  },
  yaxis: {
    title: 'Pulse'
  },
  plot_bgcolor: 'black',
 
};
const config = {
  responsive: true
};

Plotly.plot('Chart2', [{
  type: 'scatter',
  mode: 'lines',
  x: [],
  y: [],
  line: {
    color: 'lime'
  }
}], layout, config).then(chart => {// 펄스 데이터 업데이트 
  function updateChart() {

    // 새로운 x, y 데이터 생성
    const newX =  Array.from({ length: filteredECG.length}, (_, i) => i); //
    const newY = filteredECG; 
 
      Plotly.update('Chart2', {x: [newX], y: [newY]})
    }
  //  0.5초마다 플롯(그래프) 업데이트
   setInterval(updateChart, 500);
});


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


let handleUserLeft = (MemberId)=>{ 
    document.getElementById('user-2').style.display = 'none'
    guest = false;
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
    guest =true;
}


let createPeerConnection = async (MemberId) => {
  peerConnection = new RTCPeerConnection(servers)
  console.log(peerConnection)
  dataChannel = peerConnection.createDataChannel("data_Channel")

  remoteStream = new MediaStream()
  document.getElementById('user-2').srcObject = remoteStream
  document.getElementById('user-2').style.display = 'block'

  if (!localStream) {
    localStream = await navigator.mediaDevices.getUserMedia({
      video: true,
      audio: true
    })
    document.getElementById('user-1').srcObject = localStream
  }

  localStream.getTracks().forEach((track) => {
    peerConnection.addTrack(track, localStream)
  })

  peerConnection.ontrack = (event) => {
    event.streams[0].getTracks().forEach((track) => {
      remoteStream.addTrack(track)
    })
  }

  peerConnection.onicecandidate = async (event) => {
    if (event.candidate) {
      client.sendMessageToPeer({ text: JSON.stringify({ 'type': 'candidate', 'candidate': event.candidate }) }, MemberId)
    }
  }

  peerConnection.ondatachannel = (event) => {
    dataChannel.readyState = "open";
    const channel = event.channel;
    console.log('Got data channel', channel);
    console.log("data channel open: ", event.data);

    channel.onmessage = (message) => {
      if(message.data.startsWith('DATA:')){ 
        
      const receivedValue =message.data.slice(5,);
      let data = receivedValue.trim().split(',');
      
      movement.push(data[0]);
      heartRate.push(data[1]);
      ecgHeartRateMean.push(data[2]);
      sp02.push(data[3]);
      
      }
      else if(message.data.startsWith('ECG:')){
        const receivedValue = message.data.slice(4);
        ecg.push(receivedValue);
        if(ecg.length>250){
         filtering()
         ecg.shift();
        }
        while(filteredECG.length > 700){
          filteredECG.shift();
        }
      }
      else if(message.data.startsWith('start!')){
        ecg = [];
        filteredECG = [];
        heartRate = [];
        ecgHeartRateMean = [];
        sp02 = []
      }
      else if (message.data.startsWith('stop measurement')){
        let hrMin = Math.min(... ecgHeartRateMean);
        let hrMax = Math.max(... ecgHeartRateMean);
        let movementMax = Math.max(... movement)
        let sp02Min = Math.max(... sp02)
        alert('측정종료')
        measurementResult = '최소 심박수(HR):' + hrMin + '\t최대:' +hrMax+'(정상범주:60~100)\n산소포화도(sp02): ' + sp02Min + '\n손떨림' + movementMax;
        diagnosis.value = measurementResult;
        
      }
        
  }};
  
}


const N = 250;
let coefficients= [
  3.7006e-05,-0.0002278,-2.3044e-05,0.00017607,-0.00010805,-0.00024521,7.8963e-05,0.00011083,-0.00026044,-0.00020115,0.00015977,-3.5402e-05,-0.00039086,-9.5625e-05,0.00017466,-0.00026129,-0.0004567,4.794e-05,7.0407e-05,-0.00053402,-0.00041627,0.0001689,-0.00019207,-0.00078195,-0.00025752,0.00017251,-0.00060555,-0.00090992,-2.835e-05,-4.1239e-05,-0.0010927,-0.00083991,0.00015173,-0.00052788,-0.0015105,-0.00056418,0.00011554,-0.0012479,-0.0016937,-0.00018531,-0.00028997,-0.0020438,-0.0015299,8.4522e-05,-0.001122,-0.0026683,-0.0010387,-1.9033e-05,-0.0022756,-0.0028686,-0.00041493,-0.0007072,-0.0034706,-0.0025034,-4.0658e-06,-0.0020198,-0.0043179,-0.001649,-0.00019712,-0.0037457,-0.0044566,-0.0006394,-0.0012702,-0.0054313,-0.0037236,-4.3675e-06,-0.003224,-0.0064978,-0.0022875,-0.00030161,-0.0056935,-0.0064478,-0.00068035,-0.0018875,-0.0079862,-0.0050977,0.00031441,-0.0047053,-0.0092653,-0.0027468,-9.2818e-05,-0.0081807,-0.008842,-0.00019946,-0.0023825,-0.011292,-0.0064875,0.0014,-0.0064455,-0.012829,-0.0026541,0.00090113,-0.011448,-0.011776,0.0014921,-0.0024375,-0.015911,-0.00773,0.0042661,-0.0085781,-0.018012,-0.0011947,0.0038462,-0.016498,-0.016015,0.0063209,-0.0012879,-0.024206,-0.008666,0.012409,-0.01208,-0.028887,0.004751,0.013841,-0.029208,-0.026681,0.025425,0.0054995,-0.055687,-0.009169,0.062226,-0.032004,-0.1241,0.10569,0.47426,0.47426,0.10569,-0.1241,-0.032004,0.062226,-0.009169,-0.055687,0.0054995,0.025425,-0.026681,-0.029208,0.013841,0.004751,-0.028887,-0.01208,0.012409,-0.008666,-0.024206,-0.0012879,0.0063209,-0.016015,-0.016498,0.0038462,-0.0011947,-0.018012,-0.0085781,0.0042661,-0.00773,-0.015911,-0.0024375,0.0014921,-0.011776,-0.011448,0.00090113,-0.0026541,-0.012829,-0.0064455,0.0014,-0.0064875,-0.011292,-0.0023825,-0.00019946,-0.008842,-0.0081807,-9.2818e-05,-0.0027468,-0.0092653,-0.0047053,0.00031441,-0.0050977,-0.0079862,-0.0018875,-0.00068035,-0.0064478,-0.0056935,-0.00030161,-0.0022875,-0.0064978,-0.003224,-4.3675e-06,-0.0037236,-0.0054313,-0.0012702,-0.0006394,-0.0044566,-0.0037457,-0.00019712,-0.001649,-0.0043179,-0.0020198,-4.0658e-06,-0.0025034,-0.0034706,-0.0007072,-0.00041493,-0.0028686,-0.0022756,-1.9033e-05,-0.0010387,-0.0026683,-0.001122,8.4522e-05,-0.0015299,-0.0020438,-0.00028997,-0.00018531,-0.0016937,-0.0012479,0.00011554,-0.00056418,-0.0015105,-0.00052788,0.00015173,-0.00083991,-0.0010927,-4.1239e-05,-2.835e-05,-0.00090992,-0.00060555,0.00017251,-0.00025752,-0.00078195,-0.00019207,0.0001689,-0.00041627,-0.00053402,7.0407e-05,4.794e-05,-0.0004567,-0.00026129,0.00017466,-9.5625e-05,-0.00039086,-3.5402e-05,0.00015977,-0.00020115,-0.00026044,0.00011083,7.8963e-05,-0.00024521,-0.00010805,0.00017607,-2.3044e-05,-0.0002278,3.7006e-05
]
let window1 = 0.0
let filteredECG=[];

function filtering(){
  for(let i = 0 ; i < N-1 ; i++){
    window1 += ecg[i] * coefficients[i]; 
  }
  
  filteredECG.push(window1);
  window1 = 0;
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

sendBtn.addEventListener('click', ()=>{
  dataChannel.send(diagnosis.value)
  if(dataChannel.readyState==='open'){
    alert('진료기록을 보냈습니다')
    const save = confirm("저장하시겠습니까?")
    if (save) {
      let currentDate = new Date();
      const patientName = prompt('환자 이름을 입력하세요')
      const fileName =  currentDate.toISOString().slice(0,10) + patientName + '_진료기록.txt'
      
      saveAsFile(fileName)
      alert(fileName+ "저장완료")
      diagnosis.value = '';
    }
  }
  else{
    alert('연결돤 상대가 없습니다.')
  }
})

function saveAsFile(fileName){
  let diagnosis = document.getElementById('diagnosis').value;
  let blob = new Blob([diagnosis], {type: 'text/plain'})
  let link = document.createElement('a');
  link.href = window.URL.createObjectURL(blob);
  link.download = fileName;
  link.click();
}


init()