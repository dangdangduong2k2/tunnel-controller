const $ = id => document.getElementById(id), api = '/tunnel/admin/api/';
let data = null, loading = false, pending = null, messageTimer, checking = false;
const labels = {sending:'Đang gửi lệnh',accepted:'Bo đã nhận lệnh',downloading:'Đang tải',verifying:'Đang kiểm tra',verify_boot:'Kiểm tra sau khởi động',boot_verifying:'Kiểm tra sau khởi động',rebooting:'Đang khởi động lại',success:'Thành công',failed:'Thất bại',rejected:'Bo từ chối',rolled_back:'Đã về bản trước',interrupted:'Bị gián đoạn',unknown:'Chưa rõ kết quả'};
const selected = () => data?.devices.find(d => d.device_id === $('device').value);
function toast(text) { $('message').textContent = text; clearTimeout(messageTimer); messageTimer = setTimeout(() => $('message').textContent = '',7000); }
async function request(path, options={}) {
  const r = await fetch(api + path,{...options,headers:{'X-Tunnel-Request':'1',...options.headers}});
  const body = await r.json(); if (!r.ok) throw Error(body.error || 'Không thực hiện được yêu cầu.'); return body;
}
const post = (path,body) => request(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
function cell(row,text,tag='td') { const e=document.createElement(tag); e.textContent=text; row.append(e); return e; }
function options(id,values,placeholder) {
  const select=$(id), old=select.value;
  const next=values.length ? values : [{value:'',text:placeholder}];
  if (JSON.stringify([...select.options].map(o=>({value:o.value,text:o.textContent}))) === JSON.stringify(next)) return;
  select.replaceChildren();
  for (const v of next) { const o=document.createElement('option');o.value=v.value;o.textContent=v.text;select.append(o); }
  if (next.some(v=>v.value===old)) select.value=old;
}
function renderDevice() {
  const d=selected();
  $('device-name').textContent=d ? d.name : 'Chưa có thiết bị';
  $('root').textContent=d ? d.root : 'Chờ bản tin nhận diện từ bo.';
  $('version').textContent=d ? `${d.firmware} (bo tự báo)` : '—';
  $('online').textContent=!d ? 'Đang chờ' : d.conflict ? 'Trùng topic root' : d.online ? 'Online' : 'Chưa có tín hiệu mới';
  $('online').className='badge '+(d?.online&&!d.conflict?'good':d?'bad':'');
  $('last-seen').textContent=d ? 'Nhận heartbeat gần nhất: '+(d.last_live ? new Date(d.last_live*1000).toLocaleString('vi-VN') : 'Chưa nhận bản tin trực tiếp; chỉ có thông tin đã lưu.') : '';
  $('check').disabled=!d||d.conflict||checking;
  const deviceJobs=d ? data.jobs.filter(j=>j.device_id===d.device_id||j.root===d.root) : [];
  const busy=deviceJobs.find(j=>j.active);
  const latest=deviceJobs[0];
  const release=data.releases.find(r=>r.sha===$('release').value);
  const bootstrap=d&&d.firmware<1160;
  $('deploy').disabled=!data.ready||!release||release.image_schema!==1||!d||!d.online||d.conflict||busy||bootstrap;
  const note=!d ? 'Chưa phát hiện bo. Bo cần firmware hỗ trợ discovery và kết nối cùng broker.'
    : d.conflict ? 'Có nhiều bo dùng chung topic root. Cần đặt root riêng cho từng bo.'
    : !d.online ? 'Đang chờ heartbeat mới từ bo; chưa thể gửi cập nhật.'
    : bootstrap ? 'Bo cần nạp 1.16.0 qua USB một lần để sử dụng OTA mới.'
    : busy?.state==='unknown' ? 'Chưa xác định được kết quả lượt trước. Bấm Kiểm tra bo để đối chiếu trước khi gửi tiếp.'
    : busy ? `${labels[busy.state]||'Đang cập nhật'} · ${busy.percent}%. Trang tự theo dõi kết quả; chờ lượt này kết thúc trước khi gửi tiếp.`
    : !data.ready ? 'Chưa sẵn sàng gửi: cần cấu hình URL HTTPS và khóa ký trên server.'
    : release&&release.image_schema!==1 ? 'File cũ chưa có nhận diện OTA. Tải bản 1.16.0 trở lên.'
    : latest?.state==='failed'&&latest.detail==='https_connect' ? 'Lượt cập nhật đã kết thúc: bo không kết nối được server HTTPS để tải file (https_connect). Cần kiểm tra đường tải trước khi thử lại.' : '';
  $('setup').hidden=!note; $('setup').textContent=note;
}
async function refresh() {
  if(loading)return;loading=true;
  try {
    data=await request('overview');$('broker').textContent=data.broker;
    $('server-status').textContent=data.setup?.ready ? 'Server đã có URL tải và khóa ký.' : (data.setup?.problems||[]).join(' ');
    const old=$('device').value;
    options('device',data.devices.map(d=>({value:d.device_id,text:`${d.name} · ${d.online?'Online':'Chưa có tín hiệu mới'}`})),'Chưa phát hiện bo nào');
    if(old!==$('device').value){$('status-note').textContent='';$('fota').textContent='Chưa kiểm tra';}
    $('discovery-note').textContent=data.discovery.connected
      ? `Đang lắng nghe MQTT · Đã phát hiện ${data.devices.length} bo. Heartbeat 15 giây.`
      : 'Bộ phát hiện chưa kết nối broker. Đang tự kết nối lại…';
    options('release',data.releases.map(r=>({value:r.sha,text:`${r.name} · v${r.version} · ${(r.size/1024).toFixed(0)} KB`})),'Chưa có firmware');
    renderDevice();
    const host=$('history');host.replaceChildren();
    if(!data.jobs.length){host.className='empty';host.textContent='Chưa có lượt cập nhật.';}
    else {
      host.className='table-scroll';const t=document.createElement('table'),thead=t.createTHead(),h=thead.insertRow();
      ['Thời gian','Thiết bị','Phiên bản','Trạng thái','Tiến độ'].forEach(x=>cell(h,x,'th'));
      const body=t.createTBody();
      for(const j of data.jobs){const row=body.insertRow();cell(row,new Date(j.created*1000).toLocaleString('vi-VN'));cell(row,j.device_id||j.root);cell(row,String(j.version));
        const status=cell(row,labels[j.state]||j.state);if(j.detail){const small=document.createElement('small');small.textContent=j.detail;status.append(small);}
        const p=cell(row,j.percent+'% '),bar=document.createElement('progress');bar.className='progress';bar.max=100;bar.value=j.percent;bar.setAttribute('aria-label','Tiến độ tải firmware');p.append(bar);}
      host.append(t);
    }
  }catch(e){toast(e.message);}finally{loading=false;}
}
$('device').onchange=()=>{$('status-note').textContent='';$('fota').textContent='Chưa kiểm tra';renderDevice();};
$('release').onchange=()=>{if(data)renderDevice();};
$('check').onclick=async()=>{
  const d=selected();if(!d)return;const id=d.device_id,b=$('check');checking=true;b.disabled=true;b.textContent='Đang kiểm tra…';
  try{const s=await post('status',{device_id:id});if(selected()?.device_id===id){
    $('fota').textContent=s.online?(s.ota?(s.ota.configured?'Đã cấu hình':'Chưa cấp khóa/URL'):'Chưa nhận trạng thái FOTA'):'Chưa nhận phản hồi';
    $('status-note').textContent=(s.error||`Bo phản hồi · Firmware ${s.machine.firmware}`)+' · '+new Date(s.checked*1000).toLocaleTimeString('vi-VN');}
    await refresh();
  }catch(e){toast(e.message);}finally{checking=false;b.textContent='Kiểm tra bo';renderDevice();}
};
$('upload').onsubmit=async e=>{e.preventDefault();const b=$('upload-btn');b.disabled=true;b.textContent='Đang tải…';
  try{const r=await request('upload',{method:'POST',body:new FormData(e.target)});await refresh();$('release').value=r.sha;renderDevice();toast(`Đã lưu firmware ${r.version||''}. Chưa gửi lệnh cập nhật.`);}
  catch(e){toast(e.message);}finally{b.disabled=false;b.textContent='Tải lên';}};
$('deploy').onclick=()=>{const r=data.releases.find(x=>x.sha===$('release').value),d=selected();if(!r||!d)return;
  pending={sha:r.sha,device_id:d.device_id,request_id:crypto.randomUUID().replaceAll('-','')};
  const action=r.version<d.firmware?'Hạ phiên bản':r.version===d.firmware?'Nạp lại':'Cập nhật';
  $('confirm-text').textContent=`${action}: ${d.name} · ${d.firmware} → ${r.version}. File: ${r.name}.`;$('confirm').showModal();};
$('cancel').onclick=()=>$('confirm').close();
$('send').onclick=async()=>{$('send').disabled=true;try{await post('deploy',pending);$('confirm').close();toast('Đã tạo lượt cập nhật. Theo dõi kết quả bên dưới.');await refresh();}
  catch(e){toast(e.message);await refresh();}finally{$('send').disabled=false;}};
refresh();setInterval(()=>{if(!document.hidden)refresh();},3000);
