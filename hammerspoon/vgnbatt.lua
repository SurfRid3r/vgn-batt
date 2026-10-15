-- vgnbatt.lua — VGN 鼠标电量监控 (2.4G 接收器 Compx 3554:f503)
--
-- 接收器插入 → 按电量动态间隔轮询 (电量越足查得越稀):
--   ≥70%: 1h | 40-69%: 30min | 阈值~39%: 10min | <阈值: 5min | 充电中: 10min
--   查询无响应 (鼠标睡眠): 60s 起指数退避, 上限 10min, 避免夜间频繁唤醒
-- 电量 < LOW_THRESHOLD 且未充电 → 🪫 通知 + 红色屏幕提示 (固定每 1h 一次, 格式统一)
--
-- 手动: hs -c 'vgnbatt.query()'   立即查询一次

local M = {}

local BIN        = os.getenv("HOME") .. "/.hammerspoon/bin/vgnbatt"
local VENDOR_ID  = 0x3554          -- Compx / VGN
local PRODUCT_ID = 0xF503          -- 2.4G Receiver

M.LOW_THRESHOLD = 30               -- %: 低于此值提醒充电
M.REMIND_EVERY  = 3600             -- 秒: 低电量提醒节流 (固定 1h)

local active      = false          -- 轮询开关 (接收器在位)
local pending     = nil            -- 下一次查询的 hs.timer
local failCount   = 0              -- 连续无响应计数 (指数退避)
local lastRemind  = 0              -- 上次提醒时刻 (os.time 秒)

local ALERT_STYLE = { textColor = {hex = "#FF3B30"}, textSize = 28, radius = 12 }

-- 电量 → 距下次查询的秒数
function M.nextDelay(pct, mode)
    if mode == "充电中" then return 600 end
    if pct < M.LOW_THRESHOLD then return 300 end
    if pct < 40 then return 600  end
    if pct < 70 then return 1800 end
    return 3600
end

local function notifyLow(pct, mv)
    hs.printf("[vgnbatt] 低电量 %d%% (%d mV) — 提醒充电", pct, mv)
    hs.notify.new({
        title           = "🪫 VGN 鼠标电量低",
        informativeText = string.format("剩余 %d%%（%d mV）\n请及时连接充电线", pct, mv),
        withdrawAfter   = 15,
    }):send()
    hs.alert.show(string.format("🪫 鼠标电量低 %d%%，请充电", pct), ALERT_STYLE, 8)
end

local function scheduleNext(delay)
    if pending then pending:stop() end
    pending = hs.timer.doAfter(delay, function()
        pending = nil
        M.query()
    end)
end

function M.query()
    if M.task then return end      -- 上一轮还在跑
    M.task = hs.task.new(BIN, function(code, stdout, stderr)
        M.task = nil
        if not active then return end

        local pct, mode, mv
        if code == 0 then
            pct, mode, mv = stdout:match("(%d+)%%%s*%((.-),%s*(%d+) mV%)")
        end
        if not pct then            -- 睡眠/暂离: 指数退避重试
            failCount = failCount + 1
            local d = math.min(60 * (2 ^ (failCount - 1)), 600)
            hs.printf("[vgnbatt] 无响应 x%d, %ds 后轻试", failCount, d)
            scheduleNext(d)
            return
        end

        pct, mv = tonumber(pct), tonumber(mv)
        failCount = 0
        hs.printf("[vgnbatt] 电量 %d%% (%s, %d mV), 下次 %ds 后",
                  pct, mode, mv, M.nextDelay(pct, mode))

        if pct < M.LOW_THRESHOLD and mode ~= "充电中" then
            if os.time() - lastRemind >= M.REMIND_EVERY then
                notifyLow(pct, mv)
                lastRemind = os.time()
            end
        elseif pct >= M.LOW_THRESHOLD + 5 or mode == "充电中" then
            lastRemind = 0     -- 已回升或已插线, 下次再低立即提醒
        end
        scheduleNext(M.nextDelay(pct, mode))
    end)
    M.task:start()
end

local function isReceiver(t)
    return t.vendorID == VENDOR_ID and t.productID == PRODUCT_ID
end

local function startPolling()
    if active then return end
    active = true
    failCount = 0
    hs.printf("[vgnbatt] 接收器已连接, 开始动态间隔轮询")
    hs.timer.doAfter(3, M.query)   -- 等接收器就绪先查一次
end

local function stopPolling()
    if not active then return end
    active = false
    if pending then pending:stop(); pending = nil end
    lastRemind = 0
    hs.printf("[vgnbatt] 接收器已断开, 停止查询")
end

hs.usb.watcher.new(function(ev)
    if isReceiver(ev) then
        if ev.eventType == "added" then startPolling()
        elseif ev.eventType == "removed" then stopPolling() end
    end
end):start()

-- 启动/重载时接收器若已插着, 直接开始
for _, v in ipairs(hs.usb.attachedDevices() or {}) do
    if isReceiver(v) then startPolling() break end
end

return M
