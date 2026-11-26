// 校园二手交易系统 —— 前端逻辑（原生 JS，无框架）

const state = {
  user: null,
  screen: 'browse',
  categories: [],
  filters: { keyword: '', category: '', min: '', max: '', sort: 'latest', page: 1 },
  detail: null,
  sellerTab: 'items',
  audit: [],
  auditIndex: -1,
  reports: [],
  reportIndex: -1,
  logTarget: ''
};

const $ = (selector) => document.querySelector(selector);
const esc = (text) => String(text == null ? '' : text)
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;').replace(/'/g, '&#39;');

// 注册表单不保留输入：切换页签、刷新、从缓存恢复时都清空。
function resetRegisterForm() {
  const form = document.querySelector('#register-form');
  if (form) {
    form.reset();
  }
}

// ---------------------------------------------------------------- 基础

async function api(path, options = {}) {
  const init = { method: options.method || 'GET', headers: {} };
  if (options.body) {
    init.headers['Content-Type'] = 'application/x-www-form-urlencoded';
    init.body = new URLSearchParams(options.body).toString();
  }
  const response = await fetch(path, init);
  let payload = null;
  try {
    payload = await response.json();
  } catch (error) {
    throw new Error('服务返回了无法解析的内容');
  }
  if (!payload.ok) {
    if (response.status === 401) {
      showLogin();
    }
    throw new Error(payload.error || '请求失败');
  }
  return payload.data;
}

let toastTimer = null;
function toast(message, isError = false) {
  const node = $('#toast');
  node.textContent = message;
  node.className = 'toast' + (isError ? ' error' : '');
  node.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { node.hidden = true; }, 2800);
}

function openModal(html, onMount) {
  const root = $('#modal-root');
  root.innerHTML = `<div class="modal-backdrop"><div class="modal">${html}</div></div>`;
  root.querySelector('.modal-backdrop').addEventListener('click', (event) => {
    if (event.target.classList.contains('modal-backdrop')) closeModal();
  });
  if (onMount) onMount(root.querySelector('.modal'));
}

function closeModal() {
  $('#modal-root').innerHTML = '';
}

function statusChip(item) {
  const map = {
    published: 'chip-green', pending: 'chip-blue', reserved: 'chip-orange',
    confirmed: 'chip-orange', completed: 'chip-gray', offline: 'chip-red'
  };
  return `<span class="chip ${map[item.status] || 'chip-gray'}">${esc(item.status_label)}</span>`;
}

// ---------------------------------------------------------------- 登录

function showLogin() {
  state.user = null;
  $('#app-view').hidden = true;
  $('#login-view').hidden = false;
}

async function showApp(user) {
  state.user = user;
  $('#login-view').hidden = true;
  $('#app-view').hidden = false;
  $('#user-email').textContent = user.email;
  state.categories = await api('/api/categories');
  refreshNotifyDot();
  renderNavigation();
  if (user.is_admin) {
    go('audit');
  } else {
    go('browse');
  }
}

function renderNavigation() {
  const items = [{ id: 'browse', label: '首页' }];
  if (state.user.is_buyer) items.push({ id: 'reservations', label: '我的预约' });
  if (state.user.is_seller) {
    items.push({ id: 'publish', label: '发布商品' });
    items.push({ id: 'seller', label: '卖家中心' });
  }
  if (state.user.is_admin) {
    items.push({ id: 'audit', label: '商品审核' });
    items.push({ id: 'reports', label: '举报处理' });
    items.push({ id: 'stats', label: '日志统计' });
  }
  $('#nav').innerHTML = items.map((item) =>
    `<button class="nav-item ${state.screen === item.id ? 'active' : ''}" data-nav="${item.id}">${item.label}</button>`
  ).join('');
  const sidebarScreens = ['seller', 'audit', 'reports', 'stats'];
  const sidebar = $('#sidebar');
  if (sidebarScreens.includes(state.screen)) {
    sidebar.hidden = false;
    sidebar.innerHTML = `<div class="sidebar-title">${state.user.is_admin ? '管理后台' : '卖家中心'}</div>` +
      items.map((item) =>
        `<button class="sidebar-item ${state.screen === item.id ? 'active' : ''}" data-nav="${item.id}">${item.label}</button>`
      ).join('');
  } else {
    sidebar.hidden = true;
    sidebar.innerHTML = '';
  }
}

async function refreshNotifyDot() {
  try {
    const list = await api('/api/notifications');
    $('#notify-dot').hidden = !list.some((item) => !item.read);
  } catch (error) {
    /* 忽略 */
  }
}

// ---------------------------------------------------------------- 路由

function go(screen, payload) {
  state.screen = screen;
  if (screen === 'detail') state.detailId = payload;
  renderNavigation();
  const main = $('#main');
  main.innerHTML = '<div class="loading">加载中…</div>';
  const renderers = {
    browse: renderBrowse, detail: renderDetail, reservations: renderReservations,
    publish: renderPublish, seller: renderSeller, audit: renderAudit,
    reports: renderReports, stats: renderStats
  };
  Promise.resolve(renderers[screen] ? renderers[screen](main, payload) : null)
    .catch((error) => {
      main.innerHTML = `<div class="empty">${esc(error.message)}</div>`;
      toast(error.message, true);
    });
}

// ---------------------------------------------------------------- 商品检索

function renderBrowse(main) {
  main.innerHTML = `
    <h1 class="page-title">全部商品</h1>
    <p class="page-sub" id="browse-sub">加载中…</p>
    <div class="browse-layout">
      <aside class="filters">
        <h3>筛选</h3>
        <div class="filter-group">
          <h4>关键字</h4>
          <input class="input" id="f-keyword" placeholder="标题或描述" value="${esc(state.filters.keyword)}" />
        </div>
        <div class="filter-group">
          <h4>分类</h4>
          <select class="select" id="f-category">
            <option value="">全部分类</option>
            ${state.categories.map((c) => `<option value="${esc(c.id)}" ${state.filters.category === c.id ? 'selected' : ''}>${esc(c.name)}</option>`).join('')}
          </select>
        </div>
        <div class="filter-group">
          <h4>价格区间</h4>
          <div style="display:flex;gap:8px;align-items:center">
            <input class="input" id="f-min" placeholder="最低" value="${esc(state.filters.min)}" />
            <span style="color:#9aa0a6">—</span>
            <input class="input" id="f-max" placeholder="最高" value="${esc(state.filters.max)}" />
          </div>
        </div>
        <div class="filter-group">
          <h4>排序</h4>
          <select class="select" id="f-sort">
            <option value="latest" ${state.filters.sort === 'latest' ? 'selected' : ''}>最新发布</option>
            <option value="price_asc" ${state.filters.sort === 'price_asc' ? 'selected' : ''}>价格从低到高</option>
            <option value="price_desc" ${state.filters.sort === 'price_desc' ? 'selected' : ''}>价格从高到低</option>
          </select>
        </div>
        <button class="btn btn-primary" id="f-apply" style="width:100%">应用筛选</button>
      </aside>
      <section>
        <div class="grid" id="grid"><div class="loading">加载中…</div></div>
        <div class="pager" id="pager"></div>
      </section>
    </div>`;

  $('#f-apply').addEventListener('click', () => {
    state.filters.keyword = $('#f-keyword').value.trim();
    state.filters.category = $('#f-category').value;
    state.filters.min = $('#f-min').value.trim();
    state.filters.max = $('#f-max').value.trim();
    state.filters.sort = $('#f-sort').value;
    state.filters.page = 1;
    loadItems();
  });
  $('#f-keyword').addEventListener('keydown', (event) => {
    if (event.key === 'Enter') $('#f-apply').click();
  });
  loadItems();
}

async function loadItems() {
  const params = new URLSearchParams({
    keyword: state.filters.keyword, category: state.filters.category,
    min: state.filters.min, max: state.filters.max,
    sort: state.filters.sort, page: String(state.filters.page)
  });
  const data = await api('/api/items?' + params.toString());
  $('#browse-sub').textContent = `共 ${data.total} 件在售商品 · 按${state.filters.sort === 'latest' ? '发布时间' : '价格'}排序`;
  const grid = $('#grid');
  if (!data.items.length) {
    grid.innerHTML = '<div class="empty">没有符合条件的商品</div>';
  } else {
    grid.innerHTML = data.items.map((item) => `
      <article class="product-card">
        <div class="thumb">商品图片</div>
        <div class="product-title">${esc(item.title)}</div>
        <div class="price">¥${esc(item.price)}</div>
        <div class="product-meta">
          <span class="chip chip-gray">${esc(item.category)}</span>
          <span class="chip chip-gray">浏览 ${item.views}</span>
        </div>
        <div class="product-foot">
          ${statusChip(item)}
          <button class="btn btn-ghost btn-sm" data-detail="${esc(item.id)}">查看详情</button>
        </div>
      </article>`).join('');
    grid.querySelectorAll('[data-detail]').forEach((button) => {
      button.addEventListener('click', () => go('detail', button.dataset.detail));
    });
  }
  const pager = $('#pager');
  pager.innerHTML = `
    <span>第 ${data.page} / ${data.total_pages} 页</span>
    <button class="btn btn-ghost btn-sm" id="prev-page" ${data.page <= 1 ? 'disabled' : ''}>上一页</button>
    <button class="btn btn-ghost btn-sm" id="next-page" ${data.page >= data.total_pages ? 'disabled' : ''}>下一页</button>`;
  $('#prev-page').addEventListener('click', () => {
    state.filters.page = Math.max(1, state.filters.page - 1);
    loadItems();
  });
  $('#next-page').addEventListener('click', () => {
    state.filters.page = Math.min(data.total_pages, state.filters.page + 1);
    loadItems();
  });
}

// ---------------------------------------------------------------- 商品详情

async function renderDetail(main) {
  const item = await api('/api/item?id=' + encodeURIComponent(state.detailId));
  state.detail = item;
  const isOwner = state.user && item.seller_id === state.user.id;
  const canReserve = state.user && state.user.is_buyer && !isOwner && item.status === 'published';
  let mine = null;
  if (state.user && state.user.is_buyer && !isOwner) {
    try {
      const list = await api('/api/reservations');
      mine = list.find((entry) => entry.item_id === item.id &&
        (entry.status === 'pending' || entry.status === 'confirmed'));
    } catch (error) {
      mine = null;
    }
  }
  main.innerHTML = `
    <div class="toolbar"><button class="btn btn-ghost btn-sm" id="back">‹ 返回</button></div>
    <div class="detail-layout">
      <div>
        <div class="detail-image">商品图片（占位）</div>
        <div class="card" style="margin-top:20px">
          <h3 style="margin:0 0 12px;font-size:15px">商品描述</h3>
          <p style="color:#646a73;line-height:1.9;margin:0">${esc(item.description)}</p>
        </div>
      </div>
      <div>
        <h1 class="detail-title">${esc(item.title)}</h1>
        <div class="price detail-price">¥${esc(item.price)}</div>
        <div class="product-meta">${statusChip(item)}<span class="chip chip-gray">${esc(item.category)}</span></div>
        <div class="meta-list">
          <div>卖家：${esc(item.seller)}　浏览：${item.views} 次</div>
          <div>发布：${esc(item.created_at)}　编号：${esc(item.id)}</div>
        </div>
        ${item.reject_reason ? `<div class="notice">驳回理由：${esc(item.reject_reason)}</div>` : ''}
        <div class="notice">
          <strong>预约须知</strong><br />
          同一商品仅允许一条有效预约；预约后等待卖家确认，确认前可取消。<br />
          平台仅记录线下交易，不涉及在线支付与物流。
        </div>
        ${mine ? `<div class="notice" style="border-color:#f5dfb0;background:#fff9ec">
          <strong>你的预约状态：${esc(mine.status_label)}</strong><br />
          ${mine.status === 'confirmed'
            ? '卖家已确认交易，线下交付后请到“我的预约”点“确认完成”。'
            : '正在等待卖家确认；确认前可在“我的预约”取消。'}
          <br /><button class="btn btn-primary btn-sm" id="btn-myres" style="margin-top:10px">前往我的预约</button>
        </div>` : ''}
        <div style="display:flex;gap:12px;margin-top:18px">
          ${canReserve ? '<button class="btn btn-primary" id="btn-reserve" style="padding:14px 40px">立即预约</button>' : ''}
          ${!isOwner ? '<button class="btn btn-ghost" id="btn-report" style="padding:14px 28px">举报该商品</button>' : ''}
        </div>
        ${isOwner ? '<p class="hint">这是你发布的商品。</p>' : ''}
      </div>
    </div>`;
  $('#back').addEventListener('click', () => go('browse'));
  const myResButton = $('#btn-myres');
  if (myResButton) {
    myResButton.addEventListener('click', () => go('reservations'));
  }
  const reserve = $('#btn-reserve');
  if (reserve) {
    reserve.addEventListener('click', async () => {
      try {
        await api('/api/reserve', { method: 'POST', body: { item_id: item.id } });
        toast('预约成功，已加入“我的预约”');
        go('reservations');
      } catch (error) {
        toast(error.message, true);
      }
    });
  }
  const report = $('#btn-report');
  if (report) {
    report.addEventListener('click', () => openReportModal(item));
  }
}

function openReportModal(item) {
  openModal(`
    <h3>举报商品</h3>
    <div class="form-group">
      <label>举报原因</label>
      <select class="select" id="r-reason">
        <option value="0">虚假描述</option>
        <option value="1">重复发布</option>
        <option value="2">违禁物品</option>
        <option value="3">联系方式</option>
        <option value="4">其他</option>
      </select>
    </div>
    <div class="form-group">
      <label>举报说明</label>
      <textarea class="textarea" id="r-desc" placeholder="请描述具体问题"></textarea>
    </div>
    <div class="modal-actions">
      <button class="btn btn-ghost" id="r-cancel">取消</button>
      <button class="btn btn-primary" id="r-submit">提交举报</button>
    </div>`, (modal) => {
    modal.querySelector('#r-cancel').addEventListener('click', closeModal);
    modal.querySelector('#r-submit').addEventListener('click', async () => {
      try {
        await api('/api/report', {
          method: 'POST',
          body: { item_id: item.id, reason: modal.querySelector('#r-reason').value,
                  description: modal.querySelector('#r-desc').value }
        });
        closeModal();
        toast('举报已提交，等待管理员处理');
      } catch (error) {
        toast(error.message, true);
      }
    });
  });
}

// ---------------------------------------------------------------- 我的预约

async function renderReservations(main) {
  const list = await api('/api/reservations');
  const rows = list.map((item) => `
    <tr>
      <td><strong>${esc(item.item_title)}</strong></td>
      <td><span class="chip chip-blue">${esc(item.status_label)}</span></td>
      <td>${esc(item.created_at)}</td>
      <td>
        ${item.status === 'pending' ? `<button class="btn btn-ghost btn-sm" data-cancel="${esc(item.id)}">取消预约</button>` : ''}
        ${item.status === 'confirmed' ? `<button class="btn btn-primary btn-sm" data-complete="${esc(item.id)}">确认完成</button>` : ''}
      </td>
    </tr>`).join('');
  main.innerHTML = `
    <h1 class="page-title">我的预约</h1>
    <p class="page-sub">卖家确认前可取消；线下交付后点“确认完成”结束交易。</p>
    ${list.length ? `<table class="table">
      <thead><tr><th>商品</th><th>状态</th><th>预约时间</th><th>操作</th></tr></thead>
      <tbody>${rows}</tbody></table>` : '<div class="empty">还没有预约记录</div>'}`;
  main.querySelectorAll('[data-cancel]').forEach((button) => {
    button.addEventListener('click', () => reservationAction(button.dataset.cancel, 'cancel'));
  });
  main.querySelectorAll('[data-complete]').forEach((button) => {
    button.addEventListener('click', () => reservationAction(button.dataset.complete, 'complete'));
  });
}

async function reservationAction(id, action) {
  try {
    await api('/api/reservation/action', { method: 'POST', body: { id, action } });
    toast(action === 'cancel' ? '预约已取消' : '已确认完成');
    go('reservations');
  } catch (error) {
    toast(error.message, true);
  }
}

// ---------------------------------------------------------------- 发布商品

async function renderPublish(main) {
  main.innerHTML = `
    <h1 class="page-title">发布商品</h1>
    <p class="page-sub">填写信息 → 提交审核 → 管理员通过后上架</p>
    <div class="publish-layout">
      <section class="form-card">
        <div class="form-group">
          <label>商品标题</label>
          <input class="input" id="p-title" placeholder="品类 + 型号 + 成色" />
        </div>
        <div class="form-row">
          <div class="form-group">
            <label>分类</label>
            <select class="select" id="p-category">
              ${state.categories.map((c) => `<option value="${esc(c.id)}">${esc(c.name)}</option>`).join('')}
            </select>
          </div>
          <div class="form-group">
            <label>价格（元）</label>
            <input class="input" id="p-price" placeholder="例如 480" />
          </div>
        </div>
        <div class="form-group">
          <label>商品描述</label>
          <textarea class="textarea" id="p-desc" style="min-height:150px" placeholder="成色、使用情况、交易地点等"></textarea>
        </div>
        <div style="display:flex;gap:12px">
          <button class="btn btn-ghost" id="p-draft" style="padding:13px 28px">保存草稿</button>
          <button class="btn btn-primary" id="p-submit" style="padding:13px 40px">提交审核</button>
        </div>
      </section>
      <aside>
        <div class="card" style="margin-bottom:18px">
          <h3 style="margin:0 0 14px;font-size:15px">发布规范</h3>
          <p style="color:#646a73;line-height:2;margin:0;font-size:13px">
            · 标题写清品类、型号与成色，避免出现联系方式。<br />
            · 同一商品请勿重复发布，重复发布将被驳回。<br />
            · 价格修改后需要重新提交审核。<br />
            · 描述最多 500 字，图片暂以描述代替。
          </p>
        </div>
        <div class="tip-card">
          <strong>审核说明</strong><br />
          提交后进入管理员待审核列表，一般 1 个工作日内完成审核。<br />
          驳回时会给出理由，可在草稿中修改后重新提交。
        </div>
      </aside>
    </div>`;
  const submit = async (withSubmit) => {
    try {
      await api('/api/seller/item', {
        method: 'POST',
        body: {
          title: $('#p-title').value.trim(),
          category: $('#p-category').value,
          price: $('#p-price').value.trim(),
          description: $('#p-desc').value.trim(),
          submit: withSubmit ? '1' : '0'
        }
      });
      toast(withSubmit ? '已提交审核' : '草稿已保存');
      go('seller');
    } catch (error) {
      toast(error.message, true);
    }
  };
  $('#p-draft').addEventListener('click', () => submit(false));
  $('#p-submit').addEventListener('click', () => submit(true));
}

// ---------------------------------------------------------------- 卖家中心

async function renderSeller(main) {
  const data = await api('/api/seller/summary');
  const stats = data.stats;
  main.innerHTML = `
    <h1 class="page-title">我的商品</h1>
    <p class="page-sub">管理在售、待审核与已完成的商品</p>
    <div class="kpi-grid">
      <div class="kpi"><div class="kpi-label">在售中</div><div class="kpi-value">${stats.published}</div><div class="kpi-hint">已发布</div></div>
      <div class="kpi"><div class="kpi-label">待审核</div><div class="kpi-value">${stats.pending}</div><div class="kpi-hint">等待管理员</div></div>
      <div class="kpi"><div class="kpi-label">预约中</div><div class="kpi-value">${stats.reserved}</div><div class="kpi-hint">含待确认</div></div>
      <div class="kpi"><div class="kpi-label">已完成</div><div class="kpi-value">${stats.completed}</div><div class="kpi-hint">历史成交</div></div>
    </div>
    <div class="tabs-row">
      <button class="tab-button ${state.sellerTab === 'items' ? 'active' : ''}" data-tab="items">商品列表</button>
      <button class="tab-button ${state.sellerTab === 'reservations' ? 'active' : ''}" data-tab="reservations">待确认预约 (${data.reservations.length})</button>
    </div>
    <div id="seller-body"></div>`;
  main.querySelectorAll('[data-tab]').forEach((button) => {
    button.addEventListener('click', () => {
      state.sellerTab = button.dataset.tab;
      renderSeller(main);
    });
  });
  const body = $('#seller-body');
  if (state.sellerTab === 'reservations') {
    if (!data.reservations.length) {
      body.innerHTML = '<div class="empty">暂无待确认预约</div>';
    } else {
      body.innerHTML = `<table class="table"><thead><tr><th>商品</th><th>买家</th><th>预约时间</th><th>操作</th></tr></thead><tbody>
        ${data.reservations.map((item) => `<tr>
          <td><strong>${esc(item.item_title)}</strong></td>
          <td>${esc(item.buyer)}</td>
          <td>${esc(item.created_at)}</td>
          <td><button class="btn btn-primary btn-sm" data-confirm="${esc(item.id)}">确认交易</button></td>
        </tr>`).join('')}
      </tbody></table>`;
      body.querySelectorAll('[data-confirm]').forEach((button) => {
        button.addEventListener('click', async () => {
          try {
            await api('/api/reservation/action', { method: 'POST', body: { id: button.dataset.confirm, action: 'confirm' } });
            toast('已确认交易');
            renderSeller(main);
          } catch (error) {
            toast(error.message, true);
          }
        });
      });
    }
    return;
  }
  if (!data.items.length) {
    body.innerHTML = '<div class="empty">还没有发布商品</div>';
    return;
  }
  body.innerHTML = `<table class="table"><thead><tr><th>商品</th><th>价格</th><th>状态</th><th>浏览</th><th>操作</th></tr></thead><tbody>
    ${data.items.map((item) => `<tr>
      <td><strong>${esc(item.title)}</strong><br /><span style="color:#9aa0a6;font-size:12px">${esc(item.id)}</span></td>
      <td class="price">¥${esc(item.price)}</td>
      <td>${statusChip(item)}</td>
      <td>${item.views}</td>
      <td>
        ${item.status === 'draft' ? `<button class="btn btn-primary btn-sm" data-submit="${esc(item.id)}">提交审核</button>` : ''}
        ${item.status === 'published' || item.status === 'reserved' ? `<button class="btn btn-ghost btn-sm" data-offline="${esc(item.id)}">下架</button>` : ''}
        <button class="btn btn-ghost btn-sm" data-view="${esc(item.id)}">查看</button>
      </td>
    </tr>`).join('')}
  </tbody></table>`;
  body.querySelectorAll('[data-submit]').forEach((button) => {
    button.addEventListener('click', () => sellerAction(button.dataset.submit, 'submit', main));
  });
  body.querySelectorAll('[data-offline]').forEach((button) => {
    button.addEventListener('click', () => sellerAction(button.dataset.offline, 'offline', main));
  });
  body.querySelectorAll('[data-view]').forEach((button) => {
    button.addEventListener('click', () => go('detail', button.dataset.view));
  });
}

async function sellerAction(id, action, main) {
  try {
    await api('/api/seller/action', { method: 'POST', body: { id, action } });
    toast(action === 'submit' ? '已提交审核' : '商品已下架');
    renderSeller(main);
  } catch (error) {
    toast(error.message, true);
  }
}

// ---------------------------------------------------------------- 商品审核

async function renderAudit(main) {
  state.audit = await api('/api/audit');
  state.auditIndex = state.audit.length ? 0 : -1;
  main.innerHTML = `
    <h1 class="page-title">商品审核</h1>
    <p class="page-sub">待审核 ${state.audit.length} 件 · 驳回必须填写理由</p>
    <div class="split">
      <div id="audit-list"></div>
      <div id="audit-detail"></div>
    </div>`;
  renderAuditList(main);
}

function renderAuditList(main) {
  const list = $('#audit-list');
  if (!state.audit.length) {
    list.innerHTML = '<div class="empty">没有待审核商品</div>';
    $('#audit-detail').innerHTML = '';
    return;
  }
  list.innerHTML = `<table class="table"><thead><tr><th>商品</th><th>卖家</th><th>价格</th><th>提交时间</th></tr></thead><tbody>
    ${state.audit.map((item, index) => `<tr class="${index === state.auditIndex ? 'selected' : ''}" data-audit="${index}">
      <td><strong>${esc(item.title)}</strong></td>
      <td>${esc(item.seller)}</td>
      <td class="price">¥${esc(item.price)}</td>
      <td>${esc(item.created_at)}</td>
    </tr>`).join('')}
  </tbody></table>`;
  list.querySelectorAll('[data-audit]').forEach((row) => {
    row.addEventListener('click', () => {
      state.auditIndex = Number(row.dataset.audit);
      renderAuditList(main);
    });
  });
  renderAuditDetail(main);
}

function renderAuditDetail(main) {
  const item = state.audit[state.auditIndex];
  const panel = $('#audit-detail');
  if (!item) {
    panel.innerHTML = '';
    return;
  }
  panel.innerHTML = `<div class="card">
    <h2 style="margin:0 0 10px;font-size:19px">${esc(item.title)}</h2>
    <div class="price" style="font-size:22px">¥${esc(item.price)}</div>
    <div class="meta-list">
      <div>卖家：${esc(item.seller)}</div>
      <div>分类：${esc(item.category)}</div>
      <div>编号：${esc(item.id)}</div>
    </div>
    <p style="color:#646a73;line-height:1.9">${esc(item.description)}</p>
    <div class="form-group">
      <label>驳回理由（驳回时必填）</label>
      <textarea class="textarea" id="a-reason" placeholder="请说明具体原因，将同步给卖家"></textarea>
    </div>
    <div style="display:flex;gap:10px">
      <button class="btn btn-primary" id="a-approve">通过并发布</button>
      <button class="btn btn-danger" id="a-reject">驳回并退回草稿</button>
    </div>
  </div>`;
  $('#a-approve').addEventListener('click', () => auditAction(item.id, 'approve', '', main));
  $('#a-reject').addEventListener('click', () => auditAction(item.id, 'reject', $('#a-reason').value, main));
}

async function auditAction(id, action, reason, main) {
  try {
    await api('/api/audit', { method: 'POST', body: { id, action, reason } });
    toast(action === 'approve' ? '已通过并发布' : '已驳回并退回草稿');
    renderAudit(main);
  } catch (error) {
    toast(error.message, true);
  }
}

// ---------------------------------------------------------------- 举报处理

async function renderReports(main) {
  state.reports = await api('/api/reports');
  state.reportIndex = state.reports.length ? 0 : -1;
  main.innerHTML = `
    <h1 class="page-title">举报处理</h1>
    <p class="page-sub">待处理 ${state.reports.length} 条 · 判定成立将下架商品并作废预约</p>
    <div class="split">
      <div id="report-list"></div>
      <div id="report-detail"></div>
    </div>`;
  renderReportList(main);
}

function renderReportList(main) {
  const list = $('#report-list');
  if (!state.reports.length) {
    list.innerHTML = '<div class="empty">没有待处理举报</div>';
    $('#report-detail').innerHTML = '';
    return;
  }
  list.innerHTML = `<div class="list">
    ${state.reports.map((item, index) => `<div class="list-item ${index === state.reportIndex ? 'active' : ''}" data-report="${index}">
      <div class="title">${esc(item.item_title)}</div>
      <div class="meta"><span class="chip chip-red">${esc(item.reason_label)}</span> ${esc(item.reporter)} · ${esc(item.created_at)}</div>
    </div>`).join('')}
  </div>`;
  list.querySelectorAll('[data-report]').forEach((row) => {
    row.addEventListener('click', () => {
      state.reportIndex = Number(row.dataset.report);
      renderReportList(main);
    });
  });
  renderReportDetail(main);
}

function renderReportDetail(main) {
  const report = state.reports[state.reportIndex];
  const panel = $('#report-detail');
  if (!report) {
    panel.innerHTML = '';
    return;
  }
  panel.innerHTML = `<div class="card">
    <h2 style="margin:0 0 8px;font-size:19px">${esc(report.item_title)}</h2>
    <span class="chip chip-red">${esc(report.reason_label)}</span>
    <div class="meta-list">
      <div>举报人：${esc(report.reporter)}</div>
      <div>提交时间：${esc(report.created_at)}</div>
      <div>该商品历史举报 ${report.history} 条</div>
    </div>
    <p style="color:#646a73;line-height:1.9">说明：${esc(report.description)}</p>
    <div class="tip-card" style="margin-bottom:16px">
      判定成立将立即下架商品，并作废该商品的有效预约、通知买家。此操作会写入操作日志。
    </div>
    <div class="form-group">
      <label>处理备注</label>
      <textarea class="textarea" id="rp-note" placeholder="可填写处理说明"></textarea>
    </div>
    <div style="display:flex;gap:10px">
      <button class="btn btn-danger" id="rp-uphold">判定成立并下架</button>
      <button class="btn btn-ghost" id="rp-reject">判定不成立</button>
    </div>
  </div>`;
  $('#rp-uphold').addEventListener('click', () => reportAction(report.id, '1', $('#rp-note').value, main));
  $('#rp-reject').addEventListener('click', () => reportAction(report.id, '0', $('#rp-note').value, main));
}

async function reportAction(id, uphold, note, main) {
  try {
    await api('/api/reports', { method: 'POST', body: { id, uphold, note } });
    toast(uphold === '1' ? '举报成立，商品已下架' : '举报已驳回');
    renderReports(main);
  } catch (error) {
    toast(error.message, true);
  }
}

// ---------------------------------------------------------------- 统计

async function renderStats(main) {
  const stats = await api('/api/stats');
  const logs = await api('/api/logs' + (state.logTarget ? '?target=' + encodeURIComponent(state.logTarget) : ''));
  const maxCategory = Math.max(1, ...stats.categories.map((item) => item.count));
  const maxDaily = Math.max(1, ...stats.daily);
  const points = stats.daily.map((value, index) => {
    const x = 20 + index * 66;
    const y = 150 - (value / maxDaily) * 120;
    return { x, y, value };
  });
  const path = points.map((point, index) => `${index === 0 ? 'M' : 'L'}${point.x},${point.y}`).join(' ');
  main.innerHTML = `
    <h1 class="page-title">统计报表</h1>
    <p class="page-sub">统计口径：已取消预约不计入成功，已下架不计入已发布</p>
    <div class="kpi-grid">
      <div class="kpi"><div class="kpi-label">新增商品</div><div class="kpi-value">${stats.new_items}</div><div class="kpi-hint">近 7 天</div></div>
      <div class="kpi"><div class="kpi-label">预约成功</div><div class="kpi-value">${stats.successful_reservations}</div><div class="kpi-hint">近 7 天</div></div>
      <div class="kpi"><div class="kpi-label">待处理举报</div><div class="kpi-value">${stats.pending_reports}</div><div class="kpi-hint">需要处理</div></div>
      <div class="kpi"><div class="kpi-label">活跃用户</div><div class="kpi-value">${stats.active_users}</div><div class="kpi-hint">全部</div></div>
    </div>
    <div class="charts">
      <div class="chart-card">
        <div class="chart-title">各分类商品数量</div>
        <div class="bars">
          ${stats.categories.map((item, index) => `<div class="bar-item">
            <div class="bar-value">${item.count}</div>
            <div class="bar ${index % 2 ? 'alt' : ''}" style="height:${Math.max(6, (item.count / maxCategory) * 150)}px"></div>
            <div class="bar-label">${esc(item.name)}</div>
          </div>`).join('')}
        </div>
      </div>
      <div class="chart-card">
        <div class="chart-title">近 7 天预约量</div>
        <svg viewBox="0 0 460 180" style="width:100%;height:190px">
          <line x1="10" y1="150" x2="450" y2="150" stroke="#e5e8ef" />
          <path d="${path}" fill="none" stroke="#2f6bff" stroke-width="2.5" />
          ${points.map((point) => `<circle cx="${point.x}" cy="${point.y}" r="4" fill="#2f6bff" />`).join('')}
        </svg>
      </div>
    </div>
    <div class="toolbar">
      <h2 style="margin:0;font-size:18px">最近操作日志</h2>
      <div class="spacer"></div>
      <select class="select" id="log-target" style="width:140px">
        <option value="">全部对象</option>
        <option value="商品" ${state.logTarget === '商品' ? 'selected' : ''}>商品</option>
        <option value="预约" ${state.logTarget === '预约' ? 'selected' : ''}>预约</option>
        <option value="举报" ${state.logTarget === '举报' ? 'selected' : ''}>举报</option>
      </select>
    </div>
    <table class="table">
      <thead><tr><th>时间</th><th>操作人</th><th>操作</th><th>对象</th></tr></thead>
      <tbody>
        ${logs.map((log) => `<tr>
          <td>${esc(log.time)}</td>
          <td>${esc(log.operator)}</td>
          <td><strong>${esc(log.action)}</strong></td>
          <td>${esc(log.target_type)} / ${esc(log.target_id)}</td>
        </tr>`).join('') || '<tr><td colspan="4" class="empty">暂无日志</td></tr>'}
      </tbody>
    </table>`;
  $('#log-target').addEventListener('change', (event) => {
    state.logTarget = event.target.value;
    renderStats(main);
  });
}

// ---------------------------------------------------------------- 通知

async function openNotifications() {
  try {
    const list = await api('/api/notifications');
    openModal(`
      <h3>通知</h3>
      <div style="max-height:340px;overflow:auto">
        ${list.length ? list.map((item) => `<div style="padding:10px 0;border-bottom:1px solid #e5e8ef">
          <div style="color:${item.read ? '#9aa0a6' : '#2f6bff'};font-size:13px"><strong>${esc(item.type)}</strong></div>
          <div style="color:#646a73;font-size:13px;margin:4px 0">${esc(item.content)}</div>
          <div style="color:#9aa0a6;font-size:12px">${esc(item.time)}</div>
        </div>`).join('') : '<div class="empty">暂无通知</div>'}
      </div>
      <div class="modal-actions">
        <button class="btn btn-ghost" id="n-mine">我的预约</button>
        <button class="btn btn-ghost" id="n-close">关闭</button>
        <button class="btn btn-primary" id="n-read">全部标记为已读</button>
      </div>`, (modal) => {
      modal.querySelector('#n-close').addEventListener('click', closeModal);
      modal.querySelector('#n-mine').addEventListener('click', () => {
        closeModal();
        go('reservations');
      });
      modal.querySelector('#n-read').addEventListener('click', async () => {
        try {
          await api('/api/notifications/read', { method: 'POST' });
          closeModal();
          refreshNotifyDot();
          toast('已全部标记为已读');
        } catch (error) {
          toast(error.message, true);
        }
      });
    });
  } catch (error) {
    toast(error.message, true);
  }
}

// ---------------------------------------------------------------- 事件绑定

document.querySelectorAll('.tab').forEach((tab) => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach((item) => item.classList.remove('active'));
    tab.classList.add('active');
    const showRegister = tab.dataset.tab === 'register';
    $('#login-form').hidden = showRegister;
    $('#register-form').hidden = !showRegister;
    resetRegisterForm();
  });
});

$('#login-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = new FormData(event.target);
  try {
    const user = await api('/api/login', {
      method: 'POST',
      body: { account: form.get('account'), password: form.get('password') }
    });
    await showApp(user);
    toast('登录成功，欢迎 ' + user.email);
  } catch (error) {
    toast(error.message, true);
  }
});

$('#register-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = new FormData(event.target);
  try {
    await api('/api/register', {
      method: 'POST',
      body: {
        email: form.get('email'), phone: form.get('phone'),
        password: form.get('password'), seller: form.get('seller') ? '1' : '0'
      }
    });
    toast('注册成功，请登录');
    resetRegisterForm();
    document.querySelector('.tab[data-tab="login"]').click();
  } catch (error) {
    toast(error.message, true);
  }
});

document.addEventListener('click', (event) => {
  const nav = event.target.closest('[data-nav]');
  if (nav) {
    go(nav.dataset.nav);
  }
});

$('#btn-logout').addEventListener('click', async () => {
  try {
    await api('/api/logout', { method: 'POST' });
  } catch (error) {
    /* 忽略 */
  }
  showLogin();
});

$('#btn-notify').addEventListener('click', openNotifications);

// 启动：检查是否已登录
(async function boot() {
  try {
    const user = await api('/api/me');
    if (user) {
      await showApp(user);
    } else {
      showLogin();
    }
  } catch (error) {
    showLogin();
  }
})();

// 浏览器前进/后退或从往返缓存恢复时，注册表单同样保持为空。
window.addEventListener('pageshow', () => {
  resetRegisterForm();
});
