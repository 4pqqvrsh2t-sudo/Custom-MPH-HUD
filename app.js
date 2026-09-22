'use strict';

const SERVICE_UUID='c6f51001-46bb-4bb5-a8dd-000000000001';
const SPEED_UUID='c6f51002-46bb-4bb5-a8dd-000000000001';
const MPH_PER_MPS=2.2369362920544;

const $=id=>document.getElementById(id);
const encoder=new TextEncoder();

let device=null;
let speedCharacteristic=null;
let connected=false;
let writing=false;
let pendingPacket=null;

let watchId=null;
let tracking=false;
let lastFix=null;
let filteredSpeed=null;

function setStatus(id,text){$(id).textContent=text}

function distanceMeters(a,b){
  const R=6371000;
  const p=Math.PI/180;
  const x=(b.lon-a.lon)*p*Math.cos((a.lat+b.lat)*p/2);
  const y=(b.lat-a.lat)*p;
  return Math.hypot(x,y)*R;
}

function deriveMph(position){
  const c=position.coords;
  const now=position.timestamp||Date.now();

  let mph=Number.isFinite(c.speed)&&c.speed>=0
    ? c.speed*MPH_PER_MPS
    : null;

  const fix={lat:c.latitude,lon:c.longitude,t:now};

  if(mph==null&&lastFix){
    const dt=(fix.t-lastFix.t)/1000;
    if(dt>.3&&dt<6){
      mph=(distanceMeters(lastFix,fix)/dt)*MPH_PER_MPS;
    }
  }

  lastFix=fix;

  if(!Number.isFinite(mph))mph=0;
  mph=Math.max(0,Math.min(180,mph));

  if(mph<0.7)mph=0;

  if(filteredSpeed==null)filteredSpeed=mph;
  else if(mph===0)filteredSpeed=0;
  else filteredSpeed=filteredSpeed*.55+mph*.45;

  return filteredSpeed;
}

async function flush(){
  if(!connected||!speedCharacteristic||writing||pendingPacket==null)return;

  const packet=pendingPacket;
  pendingPacket=null;
  writing=true;

  try{
    const bytes=encoder.encode(packet);
    if(speedCharacteristic.properties.writeWithoutResponse&&speedCharacteristic.writeValueWithoutResponse){
      await speedCharacteristic.writeValueWithoutResponse(bytes);
    }else{
      await speedCharacteristic.writeValue(bytes);
    }
    setStatus('txStatus',packet.trim()+' mph');
  }catch(err){
    console.warn(err);
    connected=false;
    speedCharacteristic=null;
    setStatus('bleStatus','LOST');
    $('connectBtn').textContent='Connect HUD';
  }finally{
    writing=false;
    if(pendingPacket!=null)queueMicrotask(flush);
  }
}

function sendSpeed(mph){
  if(!connected)return;
  pendingPacket=mph.toFixed(1)+'\n';
  flush();
}

async function connectHud(){
  if(!navigator.bluetooth){
    setStatus('bleStatus','UNSUPPORTED');
    alert('This browser does not expose Web Bluetooth. On iPhone, use a Web Bluetooth browser such as Bluefy.');
    return;
  }

  setStatus('bleStatus','SEARCH');

  device=await navigator.bluetooth.requestDevice({
    filters:[{services:[SERVICE_UUID]}],
    optionalServices:[SERVICE_UUID]
  });

  device.addEventListener('gattserverdisconnected',()=>{
    connected=false;
    speedCharacteristic=null;
    setStatus('bleStatus','OFF');
    $('connectBtn').textContent='Connect HUD';
  });

  setStatus('bleStatus','CONNECT');

  const server=await device.gatt.connect();
  const service=await server.getPrimaryService(SERVICE_UUID);
  speedCharacteristic=await service.getCharacteristic(SPEED_UUID);

  connected=true;
  setStatus('bleStatus','LIVE');
  $('connectBtn').textContent='HUD Connected';
}

function onPosition(position){
  const mph=deriveMph(position);
  $('speed').textContent=Math.round(mph);
  $('accuracy').textContent='GPS ±'+Math.round(position.coords.accuracy||0)+' m';
  setStatus('gpsStatus','LIVE');
  sendSpeed(mph);
}

function onGeoError(err){
  console.warn(err);
  setStatus('gpsStatus','ERROR');
  $('accuracy').textContent=err.message||'GPS error';
}

function toggleGps(){
  if(tracking){
    if(watchId!=null)navigator.geolocation.clearWatch(watchId);
    watchId=null;
    tracking=false;
    lastFix=null;
    filteredSpeed=null;
    $('gpsBtn').textContent='Start GPS';
    setStatus('gpsStatus','OFF');
    $('accuracy').textContent='GPS off';
    return;
  }

  if(!navigator.geolocation){
    alert('Geolocation is not available in this browser.');
    return;
  }

  tracking=true;
  $('gpsBtn').textContent='Stop GPS';
  setStatus('gpsStatus','START');

  watchId=navigator.geolocation.watchPosition(
    onPosition,
    onGeoError,
    {enableHighAccuracy:true,maximumAge:0,timeout:10000}
  );
}

$('connectBtn').addEventListener('click',()=>connectHud().catch(err=>{
  console.warn(err);
  setStatus('bleStatus','ERROR');
}));

$('gpsBtn').addEventListener('click',toggleGps);
