/* ============================================================
 * AI 聊天助手 - 前端逻辑
 * 后端接口契约:
 *   GET    /api/sessions                      -> { success, message, data:[{id,model,lastUpdateTime,lastUserMessage,...}] }
 *   GET    /api/models                        -> { success, message, data:[{model,desc}] }
 *   POST   /api/session                       body:{model} -> { success, message, data:{session_id,model} }
 *   GET    /api/session/:id/history           -> { success, message, data:[{role,content,timestamp}] }
 *   DELETE /api/session/:id                   -> { success, message }
 *   POST   /api/message/async                 body:{sessionId,message} -> SSE:
 *              data: "<json字符串>"\n\n ... data: [DONE]\n\n
 * ============================================================ */

const $ = (id) => document.getElementById(id);

const MAX_CHARS = 2000;

const state = {
  sessions: [],
  models: [],
  currentSessionId: null,
  currentModel: null,
  selectedModel: null,      // 模型弹窗中的选择
  streaming: false,
  streamingSessionId: null,  // 正在流式生成的会话 id, 用于限制其他会话的输入
  abortController: null,
  streamBuffer: "",
  streamMessageEl: null,
  renderTimer: null,
  deleteTarget: null,       // 待删除的会话
  pinnedToBottom: true,     // 用户视角是否停留在消息底部(增量回复时跟随滚动)
};

const el = {
  sessionList: $("sessionList"),
  welcome: $("welcome"),
  messageList: $("messageList"),
  inputArea: $("inputArea"),
  input: $("messageInput"),
  send: $("btnSend"),
  charCount: $("charCount"),
  modelModal: $("modelModal"),
  modelGrid: $("modelGrid"),
  confirmModal: $("confirmModal"),
  toast: $("toast"),
};

/* ===================== 通用工具 ===================== */

// 服务端时间戳单位为秒(与后端 std::time 一致), 需转为毫秒后再构造 Date
// 统一显示年月日时分, 如 2026-08-04 14:30
function fmtTime(ts) {
  if (!ts) return "";
  const d = new Date(ts * 1000);
  const pad = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ` +
    `${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

let toastTimer = null;
function showToast(msg, ms = 2200) {
  el.toast.textContent = msg;
  el.toast.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.toast.classList.remove("show"), ms);
}

function scrollToBottom() {
  el.messageList.scrollTop = el.messageList.scrollHeight;
}

// 依据当前滚动位置判断用户是否停留在底部
function updatePinned() {
  state.pinnedToBottom =
    el.messageList.scrollHeight - el.messageList.scrollTop - el.messageList.clientHeight < 80;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[c]));
}

function copyText(text) {
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).catch(() => fallbackCopy(text));
  } else {
    fallbackCopy(text);
  }
}
function fallbackCopy(text) {
  const ta = document.createElement("textarea");
  ta.value = text;
  ta.style.position = "fixed";
  ta.style.opacity = "0";
  document.body.appendChild(ta);
  ta.select();
  try { document.execCommand("copy"); } catch (_) { /* 忽略 */ }
  document.body.removeChild(ta);
}

/* ===================== API ===================== */

const API = {
  async getModels() {
    const res = await fetch("/api/models");
    const j = await res.json();
    return j.success ? j.data : [];
  },
  async getSessions() {
    const res = await fetch("/api/sessions");
    const j = await res.json();
    return j.success ? j.data : [];
  },
  async createSession(model) {
    const res = await fetch("/api/session", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ model }),
    });
    return res.json();
  },
  async getHistory(sessionId) {
    const res = await fetch(`/api/session/${encodeURIComponent(sessionId)}/history`);
    const j = await res.json();
    return j.success ? j.data : [];
  },
  async deleteSession(sessionId) {
    const res = await fetch(`/api/session/${encodeURIComponent(sessionId)}`, {
      method: "DELETE",
    });
    return res.json();
  },
  async sendStream(sessionId, message, handlers, signal) {
    const res = await fetch("/api/message/async", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ sessionId, message }),
      signal,
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    if (!res.body) throw new Error("响应不含流式数据");

    const reader = res.body.getReader();
    const decoder = new TextDecoder("utf-8");
    let buf = "";
    let finished = false;

    const handleEvent = (line) => {
      if (!line.startsWith("data:")) return;
      const payload = line.slice(5).trim();
      if (payload === "[DONE]") {
        handlers.onDone();
        return true; // 标记流结束
      }
      if (!payload) return;
      try {
        const text = JSON.parse(payload);
        if (text) handlers.onData(String(text));
      } catch (_) { /* 忽略无法解析的帧 */ }
    };

    while (!finished) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true });
      let idx;
      while (!finished && (idx = buf.indexOf("\n\n")) !== -1) {
        const event = buf.slice(0, idx).trim();
        buf = buf.slice(idx + 2);
        if (event) finished = handleEvent(event);
      }
    }
    // 处理流结束时剩余的最后一帧(可能没有结尾空行)
    if (!finished && buf.trim()) {
      handleEvent(buf.trim());
    }
    // 服务端可能不发送 [DONE] (如模型流异常结束), 此时必须强制收尾,
    // 否则 onDone 不会被调用, 流式光标会一直闪烁
    if (!finished) {
      handlers.onDone();
    }
  },
};

/* ===================== Markdown 渲染与代码高亮 ===================== */

// 轻量语法高亮: 单趟交替正则(注释/字符串/关键字/数字), 避免嵌套替换破坏输出
function highlight(code) {
  const span = (cls, m) => `<span class="${cls}">${m}</span>`;
  return code.replace(
    /(\/\/[^\n]*|\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(\b(?:return|if|else|for|while|do|function|const|let|var|class|extends|import|from|export|default|new|delete|typeof|instanceof|in|of|try|catch|finally|throw|async|await|yield|switch|case|break|continue|def|lambda|struct|int|void|char|bool|float|double|long|unsigned|include|define|using|namespace|public|private|protected|static|virtual|override|template|typename|std|auto|enum|union|typedef|sizeof|true|false|null|None|self)\b)|(\b(?:0x[\da-fA-F]+|\d+(?:\.\d+)?)\b)/g,
    (m, comment, str, kw, num) => {
      if (comment) return span("c", comment);
      if (str) return span("s", str);
      if (kw) return span("k", kw);
      if (num) return span("n", num);
      return m;
    }
  );
}

// 行内格式化(代码/加粗/删除线/斜体/链接)
function renderInline(src) {
  let h = escapeHtml(src);
  const saved = [];
  const save = (html) => {
    const token = `\u0000${saved.length}\u0000`;
    saved.push(html);
    return token;
  };
  // 先取出 markdown 链接, 避免被下方裸链接规则二次包裹
  h = h.replace(/\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g, (m, t, u) =>
    save(`<a href="${u}" target="_blank" rel="noreferrer">${t}</a>`));
  // 裸链接自动转可点击
  h = h.replace(/(https?:\/\/[^\s<>"']+)/g, (m) =>
    save(`<a href="${m}" target="_blank" rel="noreferrer">${m}</a>`));
  // 行内代码(优先, 保护其中的其它符号)
  h = h.replace(/`([^`\n]+)`/g, '<code class="inline">$1</code>');
  // 加粗
  h = h.replace(/\*\*([^*\n]+)\*\*/g, "<strong>$1</strong>");
  // 删除线
  h = h.replace(/~~([^~\n]+)~~/g, "<del>$1</del>");
  // 斜体(避免与加粗冲突)
  h = h.replace(/(^|[^*\w])\*([^*\n]+)\*(?!\*)/g, "$1<em>$2</em>");
  // 还原链接
  return h.replace(/\u0000(\d+)\u0000/g, (_, i) => saved[+i]);
}

// 逐行块级解析: 代码围栏 / 标题 / 引用 / 列表 / 表格 / 分割线 / 段落
function markdownToHtml(src) {
  const lines = String(src).replace(/\r\n?/g, "\n").split("\n");
  const out = [];
  const n = lines.length;
  let i = 0;

  const isBlockStart = (l) =>
    /^#{1,6}\s/.test(l) || /^```/.test(l) || /^>\s?/.test(l) ||
    /^\s*[-*+]\s/.test(l) || /^\s*\d+[.)]\s/.test(l) ||
    /^\|.*\|$/.test(l) || /^(\s*[-*_]\s*){3,}$/.test(l);

  while (i < n) {
    const line = lines[i];
    if (line.trim() === "") { i++; continue; }

    // 代码围栏(未闭合时自动吸收到末尾, 兼容流式渲染)
    const fence = line.match(/^```([\w+-]*)\s*$/);
    if (fence) {
      const code = [];
      i++;
      while (i < n && !/^```/.test(lines[i])) { code.push(lines[i]); i++; }
      i++; // 跳过闭合围栏
      out.push(
        '<pre><button class="code-copy" type="button">复制</button>' +
        `<code class="hljs">${highlight(escapeHtml(code.join("\n").trimEnd()))}</code></pre>`
      );
      continue;
    }

    // 标题
    const h = line.match(/^(#{1,6})\s+(.*)$/);
    if (h) {
      out.push(`<h${h[1].length} class="md-h">${renderInline(h[2])}</h${h[1].length}>`);
      i++;
      continue;
    }

    // 分割线
    if (/^(\s*[-*_]\s*){3,}$/.test(line)) {
      out.push('<hr class="md-hr">');
      i++;
      continue;
    }

    // 引用
    if (/^>\s?/.test(line)) {
      const quote = [];
      while (i < n && /^>\s?/.test(lines[i])) {
        quote.push(lines[i].replace(/^>\s?/, ""));
        i++;
      }
      out.push(`<blockquote class="md-quote">${quote.map(renderInline).join("<br>")}</blockquote>`);
      continue;
    }

    // 有序 / 无序列表(同缩进为一组)
    const ul = line.match(/^\s*[-*+]\s+(.*)$/);
    const ol = line.match(/^\s*(\d+)([.)])\s+(.*)$/);
    if (ul || ol) {
      const indentOf = (l) => (l.match(/^\s*/) || [""])[0].length;
      const baseIndent = indentOf(line);
      if (ol) {
        // 有序列表: 保留模型输出的原始编号, 避免浏览器从 1 重新编号
        const items = [];
        while (i < n) {
          const m = lines[i].match(/^\s*(\d+)([.)])\s+(.*)$/);
          if (m && indentOf(lines[i]) === baseIndent) {
            items.push({ num: m[1], sep: m[2], text: renderInline(m[3]) });
            i++;
          } else break;
        }
        out.push(
          '<div class="md-ol">' +
            items
              .map(
                (x) =>
                  `<div class="md-li"><span class="md-num">${x.num}${x.sep}</span>` +
                  `<span class="md-text">${x.text}</span></div>`
              )
              .join("") +
            "</div>"
        );
      } else {
        const items = [];
        while (i < n) {
          const m = lines[i].match(/^\s*[-*+]\s+(.*)$/);
          if (m && indentOf(lines[i]) === baseIndent) {
            items.push(renderInline(m[1]));
            i++;
          } else break;
        }
        out.push(`<ul class="md-ul">${items.map((x) => `<li>${x}</li>`).join("")}</ul>`);
      }
      continue;
    }

    // 表格(管道表格)
    if (/^\|.*\|$/.test(line) && i + 1 < n && /^\|[\s:|-]+\|$/.test(lines[i + 1])) {
      const cells = (l) =>
        l.trim().replace(/^\|/, "").replace(/\|$/, "").split("|").map((c) => c.trim());
      const head = cells(line).map(renderInline);
      i += 2;
      const rows = [];
      while (i < n && /^\|.*\|$/.test(lines[i])) {
        rows.push(cells(lines[i]).map(renderInline));
        i++;
      }
      out.push(
        '<table class="md-table"><thead><tr>' +
        head.map((c) => `<th>${c}</th>`).join("") +
        '</tr></thead><tbody>' +
        rows.map((r) => `<tr>${r.map((c) => `<td>${c}</td>`).join("")}</tr>`).join("") +
        "</tbody></table>"
      );
      continue;
    }

    // 普通段落(合并连续行)
    const para = [];
    while (i < n && lines[i].trim() !== "" && !isBlockStart(lines[i])) {
      para.push(lines[i]);
      i++;
    }
    out.push(`<p class="md-p">${para.map(renderInline).join("<br>")}</p>`);
  }
  return out.join("\n");
}

/* ===================== 消息渲染 ===================== */

function avatarHtml(kind) {
  return kind === "ai"
    ? '<div class="msg-avatar">AI</div>'
    : '<div class="msg-avatar">我</div>';
}

function appendAssistantMessage() {
  const div = document.createElement("div");
  div.className = "msg msg-assistant";
  div.innerHTML =
    `${avatarHtml("ai")}` +
    `<div class="msg-body"><div class="msg-content streaming"></div>` +
    `<div class="msg-time"></div></div>`;
  el.messageList.appendChild(div);
  state.streamMessageEl = div.querySelector(".msg-content");
  return div;
}

function appendUserMessage(text) {
  const div = document.createElement("div");
  div.className = "msg msg-user";
  div.innerHTML =
    `<div class="msg-body"><div class="msg-content"></div>` +
    `<div class="msg-time"></div></div>` +
    `${avatarHtml("user")}`;
  div.querySelector(".msg-content").textContent = text;
  div.querySelector(".msg-time").textContent = fmtTime(Math.floor(Date.now() / 1000));
  el.messageList.appendChild(div);
  return div;
}

function renderHistory(messages) {
  el.messageList.innerHTML = "";
  for (const m of messages || []) {
    if (m.role === "user") {
      const div = appendUserMessage(m.content);
      div.querySelector(".msg-time").textContent = fmtTime(m.timestamp);
    } else {
      const div = appendAssistantMessage();
      const contentEl = div.querySelector(".msg-content");
      // 历史消息不是流式生成, 必须移除闪烁光标
      contentEl.innerHTML = markdownToHtml(m.content);
      contentEl.classList.remove("streaming");
      div.querySelector(".msg-time").textContent = fmtTime(m.timestamp);
    }
  }
  // 历史消息渲染完成后清除流式状态引用, 并将滑块移到最下方
  state.streamMessageEl = null;
  scrollToBottom();
  updatePinned();
}

function scheduleStreamRender() {
  if (state.renderTimer) return;
  state.renderTimer = setTimeout(() => {
    state.renderTimer = null;
    if (state.streamMessageEl) {
      state.streamMessageEl.innerHTML = markdownToHtml(state.streamBuffer);
      // 仅当用户停留在底部时才跟随滚动, 避免增量回复打断阅读
      if (state.pinnedToBottom) scrollToBottom();
    }
  }, 80);
}

function finishStream() {
  state.streaming = false;
  state.streamingSessionId = null;
  state.abortController = null;
  if (state.streamMessageEl) {
    state.streamMessageEl.innerHTML = markdownToHtml(state.streamBuffer);
    state.streamMessageEl.classList.remove("streaming");
    const timeEl = state.streamMessageEl.closest(".msg-body").querySelector(".msg-time");
    if (timeEl) timeEl.textContent = fmtTime(Math.floor(Date.now() / 1000));
  }
  state.streamMessageEl = null;
  updateInputState();
  if (!el.input.disabled) el.input.focus();
  if (state.pinnedToBottom) scrollToBottom();
  refreshSessions();
}

// 根据当前状态更新输入区可用性:
// - 有会话正在流式生成时, 仅允许在"正在生成的那个会话"中输入, 其他会话禁止输入
// - 流式生成期间禁止重复发送; 无会话时输入区禁用
function updateInputState() {
  const noSession = !state.currentSessionId;
  const otherStreaming =
    state.streaming && state.currentSessionId !== state.streamingSessionId;
  el.input.disabled = noSession || otherStreaming;
  el.send.disabled =
    noSession || otherStreaming || state.streaming || el.input.value.trim().length === 0;
}

async function sendMessage() {
  const text = el.input.value.trim();
  if (!text || state.streaming) return;
  if (!state.currentSessionId) {
    showToast("请先创建或选择一个对话");
    return;
  }

  appendUserMessage(text);
  el.input.value = "";
  updateCharCount();
  autoResizeInput();

  appendAssistantMessage();
  state.streaming = true;
  state.streamingSessionId = state.currentSessionId;
  state.streamBuffer = "";
  state.abortController = new AbortController();
  updateInputState();
  // 发送自己的消息时回到消息底部并固定视角
  state.pinnedToBottom = true;
  scrollToBottom();

  const streamOk = new Promise((resolve) => {
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      finishStream();
      resolve();
    };
    API.sendStream(
      state.currentSessionId,
      text,
      {
        onData: (chunk) => {
          state.streamBuffer += chunk;
          scheduleStreamRender();
        },
        onDone: () => finish(),
      },
      state.abortController.signal
    ).catch((err) => {
      if (err && err.name === "AbortError") return;
      state.streamBuffer += "\n\n> 请求中断，请重试。";
      scheduleStreamRender();
      finish();
    });
  });
  await streamOk;
}

/* ===================== 会话管理 ===================== */

function renderSessions() {
  if (!state.sessions.length) {
    el.sessionList.innerHTML = '<div class="session-empty">暂无对话</div>';
    return;
  }
  el.sessionList.innerHTML = "";
  for (const s of state.sessions) {
    const item = document.createElement("div");
    item.className = "session-item" + (s.id === state.currentSessionId ? " active" : "");
    item.innerHTML =
      `<div class="session-main">` +
      `<div class="session-title">${escapeHtml(s.lastUserMessage || "新对话")}</div>` +
      `<div class="session-meta">` +
      `<span class="session-model">${escapeHtml(s.model || "")}</span>` +
      `<span class="session-time">${fmtTime(s.lastUpdateTime || s.updated_at)}</span>` +
      `</div></div>` +
      `<button class="session-del" type="button" title="删除该会话">` +
      `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">` +
      `<polyline points="3 6 5 6 21 6"></polyline>` +
      `<path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"></path>` +
      `<path d="M10 11v6M14 11v6"></path>` +
      `<path d="M9 6V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2"></path>` +
      `</svg></button>`;
    item.addEventListener("click", () => {
      switchToSession(s.id, s.model);
    });
    item.querySelector(".session-del").addEventListener("click", (e) => {
      e.stopPropagation();
      openDeleteConfirm(s.id);
    });
    el.sessionList.appendChild(item);
  }
}

async function refreshSessions() {
  try {
    state.sessions = await API.getSessions();
  } catch (_) {
    state.sessions = [];
  }
  renderSessions();
}

async function switchToSession(sessionId, model, isNew) {
  // 切换会话时不中止正在进行的流式生成: 让当前会话在后台继续回复,
  // 仅在"非流式会话"中禁止输入 (见 updateInputState)
  state.currentSessionId = sessionId;
  state.currentModel = model || null;

  el.welcome.classList.add("hidden");
  el.messageList.classList.remove("hidden");
  el.inputArea.classList.remove("hidden");
  renderSessions();
  updateInputState();

  if (isNew) {
    el.messageList.innerHTML = "";
    state.pinnedToBottom = true;
    if (!el.input.disabled) el.input.focus();
    return;
  }
  let history = [];
  try {
    history = await API.getHistory(sessionId);
  } catch (_) { /* 保持空历史 */ }
  renderHistory(history);
  // 切回正在流式生成的会话时, 用当前缓冲区重建流式消息, 继续展示增量内容
  if (state.streaming && state.streamingSessionId === sessionId) {
    attachStreamingMessage();
  }
  // 每次进入会话都将滑块移到最下方
  scrollToBottom();
  updatePinned();
  updateInputState();
}

// 重新进入正在流式生成的会话时, 从当前缓冲区重建流式消息
function attachStreamingMessage() {
  appendAssistantMessage();
  if (state.streamBuffer) {
    state.streamMessageEl.innerHTML = markdownToHtml(state.streamBuffer);
  }
  state.streamMessageEl.classList.add("streaming");
  if (state.pinnedToBottom) scrollToBottom();
}

/* ===================== 模型选择 ===================== */

async function openModelModal() {
  state.selectedModel = null;
  let models = [];
  try {
    models = await API.getModels();
  } catch (_) { /* 忽略 */ }
  if (!models.length) {
    showToast("未获取到模型列表，请检查服务器配置");
    return;
  }
  state.models = models;
  renderModelGrid();
  el.modelModal.classList.remove("hidden");
}

function renderModelGrid() {
  el.modelGrid.innerHTML = "";
  state.models.forEach((m, i) => {
    // 兼容字段名差异: 后端返回 model/desc
    const name = m.name || m.model;
    const card = document.createElement("div");
    card.className = "model-card" + (i === 0 ? " selected" : "");
    card.innerHTML =
      `<span class="model-name">${escapeHtml(name)}</span>` +
      `<p class="model-desc">${escapeHtml(m.desc || "暂无描述")}</p>` +
      `<span class="radio"></span>`;
    card.addEventListener("click", () => {
      state.selectedModel = name;
      el.modelGrid.querySelectorAll(".model-card").forEach((c) => c.classList.remove("selected"));
      card.classList.add("selected");
    });
    if (i === 0) state.selectedModel = name;
    el.modelGrid.appendChild(card);
  });
}

async function createSession() {
  if (!state.selectedModel) {
    showToast("请选择一个模型");
    return;
  }
  el.modelModal.classList.add("hidden");
  let j;
  try {
    j = await API.createSession(state.selectedModel);
  } catch (_) {
    showToast("创建会话失败，请检查服务器");
    return;
  }
  if (!j.success || !(j.data && j.data.session_id)) {
    showToast(j.message || "创建会话失败");
    return;
  }
  const sid = j.data.session_id;
  await refreshSessions();
  switchToSession(sid, state.selectedModel, true);
}

/* ===================== 删除会话(带确认) ===================== */

function openDeleteConfirm(sessionId) {
  state.deleteTarget = sessionId;
  el.confirmModal.classList.remove("hidden");
}

function closeDeleteConfirm() {
  state.deleteTarget = null;
  el.confirmModal.classList.add("hidden");
}

async function confirmDelete() {
  const id = state.deleteTarget;
  closeDeleteConfirm();
  if (!id) return;
  try {
    const j = await API.deleteSession(id);
    if (!j.success) {
      showToast(j.message || "删除失败");
      refreshSessions();
      return;
    }
    state.sessions = state.sessions.filter((s) => s.id !== id);
    if (state.currentSessionId === id) {
      state.currentSessionId = null;
      state.currentModel = null;
      el.messageList.innerHTML = "";
      el.messageList.classList.add("hidden");
      el.inputArea.classList.add("hidden");
      el.welcome.classList.remove("hidden");
    }
    renderSessions();
    showToast("对话已删除");
  } catch (_) {
    showToast("删除失败，请检查服务器");
    refreshSessions();
  }
}

/* ===================== 输入区 ===================== */

function updateCharCount() {
  el.charCount.textContent = `${el.input.value.length}/${MAX_CHARS}`;
  updateInputState();
}

function autoResizeInput() {
  el.input.style.height = "auto";
  el.input.style.height = Math.min(el.input.scrollHeight, 160) + "px";
}

/* ===================== 侧栏宽度拖拽 ===================== */

function initResizer() {
  const resizer = $("sidebarResizer");
  const sidebar = document.querySelector(".sidebar");
  let dragging = false;

  resizer.addEventListener("mousedown", (e) => {
    if (e.button !== 0) return; // 仅左键
    e.preventDefault();
    dragging = true;
    resizer.classList.add("dragging");
    document.body.classList.add("resizing");
  });

  document.addEventListener("mousemove", (e) => {
    if (!dragging) return;
    // 最多占屏幕 40%, 最小 200px
    const w = Math.max(200, Math.min(window.innerWidth * 0.4, e.clientX));
    sidebar.style.width = w + "px";
  });

  document.addEventListener("mouseup", () => {
    if (!dragging) return;
    dragging = false;
    resizer.classList.remove("dragging");
    document.body.classList.remove("resizing");
  });
}

/* ===================== 事件绑定 ===================== */

function bindEvents() {
  $("btnNew").addEventListener("click", openModelModal);
  $("btnNew2").addEventListener("click", openModelModal);

  // 模型选择弹窗
  $("btnModelOk").addEventListener("click", createSession);
  $("btnModelCancel").addEventListener("click", () => el.modelModal.classList.add("hidden"));
  el.modelModal.addEventListener("click", (e) => {
    if (e.target === el.modelModal) el.modelModal.classList.add("hidden");
  });

  // 删除确认弹窗
  $("btnConfirmOk").addEventListener("click", confirmDelete);
  $("btnConfirmCancel").addEventListener("click", closeDeleteConfirm);
  el.confirmModal.addEventListener("click", (e) => {
    if (e.target === el.confirmModal) closeDeleteConfirm();
  });

  // 输入区
  el.input.addEventListener("input", () => {
    updateCharCount();
    autoResizeInput();
  });
  el.input.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      sendMessage();
    }
  });
  el.send.addEventListener("click", sendMessage);

  // 追踪用户是否停留在消息列表底部(滚动离开底部则暂停自动跟随)
  el.messageList.addEventListener("scroll", () => {
    state.pinnedToBottom =
      el.messageList.scrollHeight - el.messageList.scrollTop - el.messageList.clientHeight < 80;
  });

  // 代码复制(事件委托)
  el.messageList.addEventListener("click", (e) => {
    const btn = e.target.closest(".code-copy");
    if (!btn) return;
    const code = btn.parentElement.querySelector("code").textContent;
    copyText(code);
    btn.textContent = "已复制";
    setTimeout(() => (btn.textContent = "复制"), 1200);
  });
}

/* ===================== 初始化 ===================== */

function init() {
  bindEvents();
  initResizer();
  updateCharCount();
  // 页面加载后自动获取会话列表
  refreshSessions();
}

document.addEventListener("DOMContentLoaded", init);
