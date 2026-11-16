# MSGA 通知、未读、Badge 与自动标记已读：审计与执行说明

> **用途**：交给 Coding Agent 执行端到端审计、回归测试和必要修复。\
> **仓库**：`zce/make-slack-great-again`\
> **审计基线**：`dev` @
> `6d057e0bd02061348cf077f1c7f84f39c497bb6f`（2026-11-16；同步 upstream
> 至 2026-11-15）\
> **范围**：通知投递、Conversation/Thread/Activity
> 未读状态、Badge/highlight、自动标记已读及相关 Slack 配置规则。\
> **重要**：本文件记录的是基于指定 commit 的初步审计结论。Agent
> 必须检查实际 HEAD
> 和后续变更，并独立验证结论，不得把初步判断当作不可质疑的实现要求。

## 1. 目标与边界

本轮目标不是完整复刻 Slack 的所有通知功能，也不是先设计一个大型通用
Notification Framework。目标是：

1.  还原 Slack 相关通知、未读和自动已读规则。
2.  沿着事件/API → 状态存储 → 策略判断 → UI/系统通知逐条检查。
3.  找出并复现真实缺陷，先补回归测试，再做最小必要修复。
4.  保证 Conversation、Thread、Activity 及多 Workspace 状态一致。
5.  将尚未支持的 Slack 配置能力作为有优先级的 backlog，不擅自扩大范围。
6.  留下一套以后能够继续执行的 Checklist 和测试矩阵。

核心原则：

-   **Unread 是数据状态；Notification delivery
    是投递决策；Badge/highlight 是展示决策。**
    三者不能共用一个简单布尔值或同一个 `NotifyLevel` 判断。
-   Mute/DND 抑制通知，不应自动等价于清除未读数据。
-   Conversation 与 Thread 有独立阅读游标；Activity item
    的已读状态也不应被误认为 Conversation 已读。
-   服务器快照、实时事件、本地 optimistic state、手动 Mark unread
    和旧异步响应必须有明确的合并/优先级规则。
-   优先修正状态正确性，再完善配置能力；不做没有证据支持的大规模重构。

## 2. Slack 行为基线

以下是本次审计采用的 Slack 官方规则。Slack 产品会变化；若 Agent
执行时页面内容已更新，以执行时的官方帮助文档为准，并在审计记录中注明差异。

### 2.1 全局通知

-   全局通知级别：`Everything` 或 `Mentions and direct messages`。
-   Desktop 与 Mobile
    通知开关可独立配置；移动端还可有不同于桌面的通知规则。
-   用户可配置 Channel
    keywords。关键词不区分大小写，但按完整匹配触发；不关注的 Thread
    中的回复不会因为关键词而触发通知。
-   DND / Pause notifications 可以手动暂停，也可以按星期和时间段设置
    Notification schedule。
-   声音、通知预览、Huddle 等有独立设置。
-   暂停通知影响投递，不代表消息被读或未读状态被清除。

### 2.2 Conversation 级别规则

-   `All new posts`：通知频道/Group DM 的所有新消息。
-   `Just mentions`：只通知提及等重要消息。
-   `Mute and hide`：不投递该 Conversation 的普通通知。
-   额外配置可控制 Desktop notifications、Mobile notifications、Badge
    every unread message、Follow every thread。
-   全局级别与 Conversation override 必须按优先级合并；恢复 Default
    应重新继承全局配置，而不是保留旧的显式覆盖。

**Mute 的特殊规则：**

-   被静音的 Conversation 仍会在被 @mention 或收到 DM 时显示数字 Badge。
-   如果有人回复用户在静音 Conversation 中发送的消息，Threads
    入口仍应加粗。
-   因此，Mute 不能简单地让所有 unread、Badge 和 Thread attention
    一并消失。

### 2.3 Thread

-   关注的 Thread 收到新回复时会通知。
-   用户可以手动 Follow/Unfollow 单个 Thread。
-   Conversation 还可配置 Follow every thread。
-   Thread 的阅读游标与 Conversation 的 `last_read` 独立。
-   Mark Thread unread 不应被旧 Feed
    响应或正在进行的自动阅读逻辑立即清除。

### 2.4 Activity

Slack Activity 当前涵盖的类别比单纯 Mentions/Threads 更广，包括
DMs、Mentions、Threads、Channels、Reactions、Invitations、Apps、Reminders、VIP、Cleared
notifications 等；可配置 Activity
中显示哪些来源，并可过滤或建立自定义视图。

通知偏好页面说明 Mentions、Reactions、Thread replies 始终属于 Activity
的重要来源；DMs、All new posts 频道、Reminders、Suggested important
unreads 等可按偏好控制。

Activity item 的 Mark read / Clear 不等于无条件推进整个 Conversation 的
read cursor。清除未读 Activity 项目时，Slack
会将该通知标记为已读；新回复仍可作为新的通知重新出现。

### 2.5 Mark as Read

Slack 提供三种阅读偏好：

1.  从上次离开的位置开始，并标记 Conversation 已读。
2.  从最新消息开始，并标记 Conversation 已读。
3.  从最新消息开始，但保留尚未看过的消息为未读。

MSGA
应检查当前实现与这三种语义的差异；不要假设打开页面、窗口获得焦点、滚动到底部是同一件事。

### 2.6 Badge 的语义

Slack 的桌面/应用图标 Badge 通常区分： - 蓝点：有 Workspace 未读活动。 -
数字：DM、mention、channel mention、keyword、关注的 Thread
回复等重要活动。

Conversation Sidebar、Threads/Activity 导航、Workspace Rail、系统
Tray/Launcher Badge
是不同展示目标，应分别定义计数与颜色规则，避免重复计数或遗漏。

### 官方参考

-   [Configure your Slack
    notifications](https://slack.com/help/articles/201355156-Configure-Your-Slack-notifications)
-   [Guide to Slack
    notifications](https://slack.com/help/articles/360025446073-Guide-to-Slack-notifications)
-   [Manage notifications for specific channels and direct
    messages](https://slack.com/help/articles/360056534254-Manage-notifications-for-specific-channels-and-direct-messages)
-   [Mute channels and direct
    messages](https://slack.com/help/articles/204411433-Mute-channels-and-direct-messages)
-   [Pause your Slack
    notifications](https://slack.com/help/articles/214908388-Pause-your-Slack-notifications)
-   [Manage your Mark as Read
    preference](https://slack.com/help/articles/360043037853-Manage-your-Mark-as-read-preference)
-   [Get your work done from the Activity
    view](https://slack.com/help/articles/19693583638803-Get-your-work-done-from-the-Activity-view)
-   [Introducing the new Activity view in
    Slack](https://slack.com/help/articles/46751260742035-Introducing-the-new-Activity-view-in-Slack)

## 3. MSGA 当前架构与关键代码入口

以下路径以审计基线 commit 为准；Agent 应按实际 HEAD 重新定位。

  ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  层次                    当前实现 / 入口                                                                                             需要检查的责任
  ----------------------- ----------------------------------------------------------------------------------------------------------- ---------------------------------------------------------
  Slack 消息事件          `src/app/slack/slack_realtime.cpp`                                                                          实时消息、marked 事件、presence/DND 更新

  Slack 计数快照          `src/app/slack/slack_json.cpp` `toCounts()`                                                                 `has_unreads`、`mention_count`、`dm_count`、`last_read`
                                                                                                                                      的语义

  Conversation 状态       `src/app/model/types.h`、`src/app/model/store.cpp`                                                          `lastRead`、`unread`、`mentions`、`muted`、`notify`

  Conversation 同步       `src/app/slack/slack_backend.cpp` `pollUnreadCounts()` / `applyActivity()`                                  counts、roster、消息轮询之间的合并

  Thread 状态             `src/app/slack/slack_backend.cpp` `feedThread()`、`threadRead()`、`threadUnread()`                          Follow、Thread cursor、unread reply、异步保护

  Activity                `src/app/slack/slack_activity.cpp`、`src/app/screens/shell/activity_page.cpp`                               feed、分页、单项已读、Activity count

  自动已读                `src/app/screens/messages/message_list.cpp` `checkEdges()`                                                  Conversation 与 Thread 的阅读条件

  桌面通知                `src/app/screens/shell/shell_notify.cpp` `maybeNotify()` / `huddleChanged()`                                通知投递 gate、声音、去重、撤回

  Sidebar Badge           `src/app/screens/shell/sidebar.cpp` `paintsUnread()` / `redCount()`                                         bold、数字、蓝点、静音规则

  Workspace/App Badge     `src/app/screens/shell/sidebar.cpp` `workspaceAttention()`                                                  Workspace 汇总与 launcher count

  Shell 阅读状态          `src/app/screens/shell/shell.cpp` `reading()` / `updateReading()`                                           Focus、hidden、active window

  回归测试                `tests/app/core/test_slack_read.cpp`、`test_slack_realtime.cpp`、`tests/app/screens/shell/test_shell.cpp`   服务端同步、并发、UI 与通知行为
  ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 当前状态模型

-   **Conversation**：`lastRead`、`unread`、`mentions`、`muted`、`notify`。
-   **Thread**：独立 Follow/unfollow、read floor、unread reply、pending
    mark-unread 等状态。
-   **Activity**：服务端 Activity feed item 的独立 `is_unread` 和
    markRead/markUnread 操作；全局 `unreadActivity` 另有计数。
-   **Notification presentation**：Shell
    根据消息到达事件和配置决定是否投递 OS notification；Sidebar 根据
    Store 状态绘制未读、数字 Badge 和点。
-   **Workspace attention**：由 `workspaceAttention()` 聚合 Conversation
    数据；Thread-only unread 是否纳入当前汇总需要单独处理。

## 4. 初步发现：必须重新验证的重点

### P1-A：Mute 与重要 Badge 耦合

**基线证据：** - `model::Conversation::effectiveNotify()` 将 `muted`
映射为 `NotifyLevel::Nothing`；`types.h` 注释称 muted 会连 Badge
一起静音。 - `Sidebar::muted()` 将 effective level 为 Nothing 的
Conversation 视为 muted。 - `paintsUnread()` 在 muted 时直接返回
false；`redCount()` 在 muted 时直接返回 0。 - `workspaceAttention()`
也跳过 `NotifyLevel::Nothing`。 - 现有 `test_shell.cpp` 对 muted channel
mention badge 为 0 有明确断言。

**Slack 对照：** 静音 Conversation 被 mention 或收到 DM 时仍显示数字
Badge；静音 Conversation 中用户发出的消息收到回复时 Threads
入口仍应加粗。

**结论：** 与 Slack 规则存在明确差异。应拆分通知投递与 Badge
eligibility。修复时不能让静音频道的普通消息也产生数字
Badge/普通未读高亮。

### P1-B：`client.counts` 部分已读可能保留陈旧计数

**基线证据：** - `slack_json.cpp::toCounts()` 中，channel 的
`has_unreads` 是布尔提示，`mention_count` 是精确计数；DM 使用
`dm_count`。 - `SlackBackend::Read::applyActivity()` 当前对本地 `unread`
/ `mentions`
使用偏向单调增加的合并（`max`）；只有服务端确认完全读完的特定条件才会把两者清零。 -
`tests/app/core/test_slack_read.cpp`
有跨客户端"全部读完"测试，但没有覆盖 `last_read` 前进、仍有未读时精确
mention count 减少的场景。

**风险：** 用户在 Slack 另一客户端读掉部分消息后，MSGA 可能推进
`lastRead`，但保留旧的 `mentions` / unread 展示，且后续 counts
快照不一定再次触发修正。

**结论：** 高优先级，必须新增 Fake Slack 回归测试。修复时不能把 channel
`has_unreads` 布尔值误当成精确 unread
count；需明确本地已加载消息、服务器精确 mention
count、未加载消息、实时事件和旧快照的合并规则。

### P1-C：DND 状态未进入桌面消息通知决策

**基线证据：** - MSGA 已从 Slack DND/presence 事件维护用户 DND 状态。 -
`shell_notify.cpp::maybeNotify()` 的 gate 检查全局通知开关、workspace
mute、消息作者/年龄、当前阅读状态、Conversation membership 和有效
NotifyLevel，但基线代码未检查当前用户 DND。 - `huddleChanged()`
也需要独立检查 DND/暂停通知语义。

**结论：** 高优先级验证并修复。DND 应抑制投递（banner/sound），不能清除
unread 或 Badge。VIP 等暂停期间例外应单独评估，不应未经证据直接加入。

### P1-D：Thread 自动标记已读不受窗口阅读状态约束

**基线证据：** - `MessageList::checkEdges()` 的 Conversation 分支要求
`_reading`、非 `_openPending`、列表最新消息可见且没有 `unreadHeld`。 -
Thread 分支仅检查加载完成、最新回复非 pending、未被 `unreadHeld`
保护，就调用 `markThreadRead()`；没有检查 `_reading`。 -
源码注释明确写着 "An open thread reads itself --- on load and on each
new reply"。

**结论：** 这是明确的实现差异，但是否为 bug
需要产品语义决策。必须测试窗口失焦、最小化、隐藏、Thread
打开期间新回复、手动 Mark unread 等场景；结合 Slack
行为和用户体验作出决定，不能只为"统一代码"机械添加 `_reading`。

### P1-E：Thread-only unread 未进入 Workspace/App attention 汇总

**基线证据：** - Threads 导航入口直接使用 `Store::unreadThreads()`。 -
`workspaceAttention()` 只聚合 Conversation 的 unread/mentions，没有纳入
Thread-only unread。 - Slack 官方 App icon 数字 Badge 包含关注 Thread
的回复。

**结论：** 存在 Badge parity 缺口。检查 Workspace
Rail、Tray、Launcher/App icon 的实际数据路径，明确 Thread-only unread
是否计入数字以及如何避免和 Activity/Conversation 重复计数。

### P2：需要检查的配置能力差距

这些是 Slack 能力与 MSGA 当前实现的差异候选，不等于本轮必须全部实现：

-   Channel keywords（匹配规则、通知和高亮）。
-   DND / Notification schedule（含时区、跨日时间段、暂停/恢复）。
-   三种 Mark as Read preference。
-   Conversation 的 Desktop/Mobile notification override。
-   Badge every unread message。
-   Follow every thread（Conversation 级别）。
-   Activity 类型显示偏好及更完整的过滤类型。
-   Activity clear 与 mark-read 的语义、即时计数同步。
-   VIP 在 DND/focus mode 下的通知例外（如适用）。
-   Conversation Default 与显式 override 的持久化/同步语义。

先盘点已有
API、设置结构和上游兼容能力，再决定哪些是低成本必要修复，哪些应留在
backlog。不要为了追齐列表而把本轮扩张成完整 Slack 客户端功能项目。

## 5. 端到端审计 Checklist

每项必须给出：状态（Pass / Fail / Not covered /
N/A）、代码证据、测试证据、发现及后续动作。不能只勾选而没有依据。

### A. 消息事件与服务端同步

-   [ ] 实时新消息、历史加载、分页和重连是否被正确区分。
-   [ ] `client.counts` 初始快照和后续轮询是否正确更新状态。
-   [ ] `has_unreads`、`mention_count`、`dm_count`
    的精度是否被正确处理。
-   [ ] `last_read` 前进、后退、缺失、过期响应的处理是否正确。
-   [ ] `channel_marked` / `group_marked` / `im_marked`
    是否与本地状态一致。
-   [ ] Roster、Conversation history、counts 的竞态是否有保护。
-   [ ] 跨客户端部分已读、全部已读、收到新消息后是否正确收敛。
-   [ ] Reconnect、缓存恢复、Workspace 切换是否会重放或丢失 unread。

### B. Conversation Unread

-   [ ] 新消息是否增加 unread；自己发送的消息是否排除。
-   [ ] Mention、channel mention、@here、@everyone、user group
    的计数是否准确。
-   [ ] Read cursor 前进时是否减少对应 unread/mentions。
-   [ ] Mark as Read / Mark as Unread 是否正确重算并保持。
-   [ ] 手动 Mark unread 是否会被自动已读或旧响应清除。
-   [ ] 删除、编辑、广播消息变化是否会错误改变 unread。
-   [ ] Mute 与 All / Mentions / Default 是否有清晰且正确的语义。
-   [ ] 静音 Conversation 的重要 Badge 是否保留，普通消息是否仍被抑制。

### C. Thread Unread / Follow

-   [ ] 自己发起、参与、被提及、手动关注的 Thread 是否正确 Follow。
-   [ ] Unfollow 后旧 Feed 是否能重新打开 Follow 状态。
-   [ ] Conversation mute、Thread mute、Thread follow 是否彼此独立。
-   [ ] Thread read cursor 是否独立于 Conversation cursor。
-   [ ] Thread 新回复、批量回复、广播回复是否正确计数。
-   [ ] 跨客户端 Thread 已读是否同步。
-   [ ] 手动 Mark Thread unread 是否有 pending/epoch 保护。
-   [ ] 窗口失焦、最小化、隐藏时打开 Thread 是否自动已读。
-   [ ] Thread 打开期间新回复是否自动已读，是否符合决议。
-   [ ] Thread-only unread 是否进入 Threads、Workspace、Tray/App Badge。

### D. Activity

-   [ ] Mention、DM、Thread、Reaction、Channel item 的解析和分类。
-   [ ] Feed 初次加载、分页、刷新、workspace switch 的 generation 保护。
-   [ ] Unread-only 过滤是否只展示未读项。
-   [ ] 单项 Mark Read / Mark Unread 是否即时更新 row 和全局计数。
-   [ ] 打开 Activity item 后，Conversation/Thread read cursor
    是否按预期推进。
-   [ ] Activity Mark Read 是否错误清除其他通知或 Conversation unread。
-   [ ] Activity count 与 Feed item 的数量是否有明确口径。
-   [ ] 不支持的 Slack Activity 类别是否记录为明确 backlog。

### E. Desktop Notification

-   [ ] 历史加载、重连、缓存恢复不产生新消息通知。
-   [ ] 实时 Append/Insert/Arrived 只对真实新消息通知。
-   [ ] 当前窗口/Conversation 正在阅读时是否抑制通知。
-   [ ] 全局 Everything / Mentions and DMs 是否正确。
-   [ ] Conversation All / Mentions / Mute override 是否正确。
-   [ ] Mention、keyword、DM、Followed Thread 的重要性判断是否正确。
-   [ ] Workspace mute、Conversation mute、Thread mute 是否分别生效。
-   [ ] DND / Pause / Schedule 是否抑制 banner 和 sound。
-   [ ] Huddle、Reminder 是否遵循各自开关和 DND 规则。
-   [ ] 重复事件是否去重；已读后是否正确撤回对应通知。
-   [ ] 点击通知是否正确打开 Conversation 或 Thread。

### F. Badge / Highlight / Attention

-   [ ] Conversation bold/unread-only 过滤规则。
-   [ ] Conversation 红色数字与蓝点的区别和计数口径。
-   [ ] 静音 Conversation 的 mention/DM Badge。
-   [ ] Threads 和 Activity 导航入口的 unread。
-   [ ] Workspace Rail 的点/数字。
-   [ ] Tray/Launcher/App icon 数字和点。
-   [ ] 多 Workspace 汇总、workspace mute 和切换。
-   [ ] Thread-only unread 的计入规则与去重。
-   [ ] 离开频道、关闭 DM、过期 unread 的清理。

### G. 自动 Mark as Read

-   [ ] 首次打开 Conversation 时的初始定位。
-   [ ] Start where left off / Start newest / leave unseen unread
    三种策略。
-   [ ] 是否滚动到底部、最新消息是否真实可见。
-   [ ] 窗口焦点、隐藏、最小化状态。
-   [ ] 窗口重新获得焦点时是否按策略推进 cursor。
-   [ ] Thread 打开、Thread 新回复、Thread 失焦时的规则。
-   [ ] Activity/Threads 页面跳转消息后的已读行为。
-   [ ] 手动 Mark unread 的保护期限和解除条件。
-   [ ] 自动 Mark read、服务端 cursor 和本地 Store 是否最终一致。

### H. 设置与持久化

-   [ ] 全局通知级别和桌面通知开关。
-   [ ] Conversation Default / All / Mentions / Mute 的覆盖优先级。
-   [ ] Muted channels 的 Slack 同步和多客户端同步。
-   [ ] Thread Follow/Unfollow 的 Slack 同步和缓存恢复。
-   [ ] Thread mute 的本地持久化。
-   [ ] DND 状态刷新、schedule 和时区语义。
-   [ ] 设置更改后通知决策与 UI 是否即时刷新。
-   [ ] 不支持的配置是否明确标记为 backlog，而非出现半实现状态。

## 6. 必须形成的测试矩阵

至少覆盖以下状态维度，不要求做笛卡尔积；按边界和交互风险组合：

  -----------------------------------------------------------------------
  维度                                必测取值
  ----------------------------------- -----------------------------------
  Conversation type                   Channel / Private / DM / Group DM

  Notify level                        Default(All) / Default(Mentions) /
                                      All / Mentions / Nothing

  Mute                                muted / unmuted

  Message type                        normal / own / mention / keyword /
                                      DM / thread reply

  Thread                              followed / unfollowed / muted /
                                      manually unread

  Window                              focused / unfocused / minimized /
                                      hidden

  Read policy                         last position / newest-read /
                                      newest-unread

  Source                              realtime / history / counts /
                                      Activity / another client

  Timing                              in-order / stale response /
                                      concurrent read / reconnect

  Workspace                           active / inactive / muted /
                                      switched
  -----------------------------------------------------------------------

优先建立的回归场景： 1. Muted channel 收到
mention：不弹桌面通知，但仍有红色数字 Badge。 2. Muted DM
收到新消息：不弹桌面通知，但仍显示 DM 数字 Badge。 3. Slack
另一客户端部分读掉 mention：MSGA 的精确 mention Badge
下降，而非永久保留旧值。 4. DND 开启时收到普通消息、mention、DM、Thread
reply、Huddle：按 Slack 规则抑制/允许投递，但 unread 不丢失。 5. Thread
打开但窗口失焦时收到回复：按最终决议保持或推进 Thread cursor。 6.
Thread-only unread：Threads 导航与 Workspace/App Badge 按统一口径变化。
7. Mark unread 后到达旧 counts/feed 响应：本地手动状态不能被旧响应清除。
8. Activity 单项 Mark read：只影响对应 Activity 状态，不误清
Conversation/Thread 其他未读。 9. 历史加载和 reconnect：不产生重复
Desktop notification。 10. 多 Workspace：通知、未读、Badge 不串
workspace。

## 7. 执行与决策流程

### 阶段 0：保护现场与环境确认

-   检查当前 branch、HEAD、`git status`、已有 diff 和未跟踪文件。
-   不得覆盖、清理、stash 或 reset 用户已有工作。
-   检查仓库现有 Containerfile、Podman 开发环境、构建说明和测试入口。

### 阶段 1：只读链路审计

-   按 A-H Checklist 盘点所有状态生产者、消费者和转换点。
-   画出状态流，列出真实 API payload 字段和精度。
-   区分 Confirmed defect、Needs decision、Feature gap、No issue。
-   先报告最重要的证据和建议，再开始修改。

### 阶段 2：测试先行

-   为每个确认缺陷写最小 Fake Slack / UI regression test。
-   确认测试在修复前失败，且失败原因正是目标问题。
-   测试必须覆盖并发、旧响应、手动 Mark unread 和历史加载等相邻边界。

### 阶段 3：分批修复

建议顺序： 1. Conversation counts / cursor 的部分已读一致性。 2.
Mute、Mention、DM 的 Badge 与 Desktop notification 解耦。 3. DND
对通知投递的 gate。 4. Thread 自动已读语义（先完成决策）。 5.
Thread-only unread 的 Workspace/App Badge 汇总。 6. Activity
与其他边界问题。

每批保持小 diff，运行针对性测试；不要把所有问题塞进一次重构。

### 阶段 4：回归与收尾

-   运行相关单测、集成测试和项目要求的完整测试。
-   检查多 Workspace、历史加载、reconnect、通知撤回、手动 Mark unread。
-   复核 diff，清理仅本轮产生的临时文件，不碰用户原有文件。
-   更新本文件的 Checklist 状态、发现、决议和剩余 backlog。

### 独立 Subagent 决策机制

仅在有多个合理语义且会影响架构/产品行为时使用独立 subagent，模型固定为
`openai/gpt-6.1-sol#max`。建议决策题： - Channel 部分已读时，服务端
`mention_count`、布尔 `has_unreads`、本地已加载消息和 realtime event
如何合并？ - Thread 在窗口失焦/隐藏时是否应自动推进 read
cursor？如何处理三种 Mark as Read 偏好？ - Thread-only unread 应如何进入
Workspace/Launcher Badge，如何避免和 Activity/Conversation 重复计数？ -
Notification delivery、Badge eligibility、Unread state
的策略边界应如何划分？

每次给 subagent 的输入必须自包含：问题、代码证据、复现步骤、Slack
规则、候选方案、约束和需要它给出的决议。要求它比较 trade-off
并明确推荐。**不要把仓库探索、全量审计、实现、测试或高消耗任务交给
subagent；主 Agent 自己完成这些工作。** Subagent 的意见是决策输入，主
Agent 必须核对代码和约束后作最终判断。

## 8. 环境与副作用约束

**严格禁止对宿主机做环境性副作用：** -
不安装、升级或卸载宿主机依赖、SDK、编译器、工具或系统包。 -
不修改宿主机系统配置、服务、环境变量、Shell
配置、dotfiles、全局缓存或用户级服务。 -
不在宿主机执行项目编译、测试或需要额外依赖的构建命令。 - 不使用 sudo
在宿主机进行任何环境配置。

**所有本地环境、依赖、编译和测试均通过 Podman 集中完成：** -
优先复用仓库现有 Containerfile/devcontainer/Podman 配置。 -
源码目录可挂载进容器用于开发；构建目录和依赖缓存放容器或项目明确忽略的临时目录。 -
不得为了方便切换到宿主机安装或测试。 -
若容器构建受阻，报告具体阻塞和可选方案，等待决策或在既有容器能力内解决。

允许对目标仓库进行本任务所需的源码和测试修改；这不授权修改仓库之外的宿主机环境。未经另行授权，不
commit、push、创建 PR、合并或发布。

## 9. 完成交付标准

-   [ ] 端到端状态链路图和规则矩阵。
-   [ ] A-H Checklist 每项均有状态和证据。
-   [ ] 已确认缺陷均有可复现回归测试。
-   [ ] 必须修复的问题已完成最小修复。
-   [ ] 关键决策有记录：问题、证据、选项、subagent 建议、主 Agent
    决定、理由。
-   [ ] Podman 环境、命令、镜像/Containerfile 来源和测试结果可复现。
-   [ ] 所有相关测试通过；未通过项明确说明。
-   [ ] 剩余 Slack parity gaps 按优先级进入 backlog。
-   [ ] 最终 diff 无无关重构、无宿主机环境副作用、无未经授权的 GitHub
    发布动作。

------------------------------------------------------------------------

## 10. Agent Goal 提示词（4000 字符以内）

``` text
在仓库 zce/make-slack-great-again 中，完成通知、未读、红点和自动标记已读逻辑的端到端审计与必要修复。基线：dev @ 6d057e0bd02061348cf077f1c7f84f39c497bb6f；开始前确认实际 HEAD、工作区状态及是否已有后续提交，不能覆盖用户改动。

以随任务提供的 `MSGA-notification-unread-audit.md` 为执行依据；若仓库中已有该文件则先检查其内容和改动，不得覆盖。若无法读取随附文档，先报告阻塞，不要臆造 Checklist。先自行阅读代码、现有测试和 Slack 官方规则，建立“事件/服务数据 → Store 状态 → 策略判断 → UI/系统通知”的链路图及规则矩阵。逐项执行文档 Checklist，标记通过、失败、未覆盖、暂不适用，并给出代码位置和证据。不要只按文档结论照单全收；重新验证每个发现，区分确定缺陷、行为待决和功能差距。

优先修复已证实的正确性问题：Mute 下重要未读 Badge、client.counts 部分已读/游标前进后的状态回退、DND 对桌面通知的抑制、Thread 自动已读的焦点/阅读语义、Thread unread 对 Workspace/App Badge 的贡献。逐项先写能失败的回归测试，再做最小修复。特别注意 Slack `client.counts` 对 channel 的 `has_unreads` 是布尔提示、`mention_count` 是精确计数；不要把它们误当成相同精度，也不要用简单 max 永久保留过期值。处理异步旧响应、实时事件、optimistic state、手动 Mark unread 和本地缓存之间的竞争，避免修复一个链路破坏另一个链路。

建立清晰的分层：Unread/read cursor 是数据状态；通知投递（banner/sound）与 Badge/highlight 是不同策略输出。不要未经证据就引入大型 NotificationPolicy 重构。Slack 有而 MSGA 尚未支持的配置（关键词、DND schedule、Mark as Read 三种偏好、Follow every thread、Activity 类型偏好等）先记录优先级和依赖；只有属于本轮必要修复、现有 API 可可靠支持且实现范围明确时才增加功能，不要扩张为完整 Slack 复刻。

对关键产品/架构决策（例如 Thread 在窗口失焦时是否自动已读、服务器计数与本地消息如何合并、Thread 对 App Badge 的计数口径），整理明确问题，附代码证据、复现、备选方案和取舍，使用独立 subagent `openai/gpt-6.1-sol#max` 获取决议。只把决策题交给 subagent；代码探索、仓库盘点、实现、测试和高消耗工作由主 Agent 自己完成。不要为普通实现细节频繁调用模型。

执行顺序：①保护现场并确认基线；②梳理链路与 Slack 行为矩阵；③补回归测试并验证缺陷；④按依赖分阶段修复；⑤运行针对性测试及完整相关测试；⑥复核 diff、更新文档与未完成项。每阶段报告改动、测试、风险和下一步。保持改动聚焦，不做无关重构，不提交/推送/合并/发布，除非另行授权。

环境约束严格执行：禁止在宿主机安装、升级或配置任何开发依赖；禁止修改宿主机系统配置、服务、环境变量、全局工具或 dotfiles；禁止在宿主机执行编译和测试。所有配置、依赖、编译、测试均在 Podman 容器内完成，优先复用仓库现有 Containerfile/开发容器配置；源码通过挂载工作区访问，产物留在容器或仓库明确的临时/忽略目录。不得为方便而绕过此约束。若容器方案受阻，先报告阻塞和最小可行方案，不得转到宿主机安装或测试。

最终交付：修复代码和回归测试；完成的 Checklist；Slack 规则与 MSGA 行为差异表；关键决策记录（含 subagent 结论及主 Agent 判断）；Podman 测试命令和结果；剩余风险/后续 backlog。
```

------------------------------------------------------------------------

## 11. 执行结果（2026-11-16）

### 11.0 基线与现场

- 实际 `HEAD`：`6d057e0bd02061348cf077f1c7f84f39c497bb6f`，与任务基线一致；`dev` 无本地领先提交。
- 开始时工作区唯一改动：未跟踪的 `docs/MSGA-notification-unread-audit.md`（本文件）。未 `reset`、`stash`、`clean` 或覆盖该文件；本轮仅在其末尾追加结果。
- 所有编译、测试均使用 Podman 镜像 `localhost/msga-pr41-quality:20261108`（Ubuntu 24.04，含 CMake/Ninja/GCC、FreeType/HarfBuzz/Wayland/XCB/D-Bus 开发包），源码以 `/workspace` 挂载，构建目录在容器侧 `/build`，没有在宿主机编译或执行测试。

### 11.1 端到端链路图

``` text
Slack HTTP/RTM/Socket
  ├─ client.counts ──> mapjson::toCounts() ──> Read::applyActivity()
  │                       (channel: has_unreads 布尔提示 + mention_count 精确；
  │                        IM: dm_count 精确)
  ├─ conversations.history / replies / subscriptions.thread.*
  │                       └─> Store::addPage()/addMessage()/announceReply()
  ├─ channel_marked / group_marked / im_marked / mpim_marked
  │                       └─> Store::updateConversation(lastRead, unread, mentions)
  ├─ message / thread reply 实时事件
  │                       └─> Read::inject() ──> Store::addMessage()/announceReply()
  │                                             └─> Store::unreadThreads(thread map)
  └─ dnd_updated / dnd_updated_user ──> User::dnd（自己/他人）

Store（唯一数据状态）
  ├─ Conversation: lastRead, latest, unread, mentions, muted, notify
  ├─ Thread: read floor / baseline / pending mark-unread（backend）
  ├─ Store::unreadThreadCounts（新增，按 conversation 的未读关注线程数）
  └─ Activity: 独立 is_unread + unreadActivity

策略输出（彼此分离）
  ├─ Shell::maybeNotify()：全局通知、workspace mute、DND、Conversation level、
  │                         mention/DM/followed thread、阅读中/旧消息/去重
  ├─ Shell::huddleChanged()/notifyReminderDue()：各自开关 + DND
  ├─ Sidebar::paintsUnread()/redCount()：bold / 数字 / 蓝点，含 Mute 重要未读
  ├─ Sidebar::workspaceAttention()：per-conversation max(DM/mention, thread)
  └─ Shell::updateAttention()：Workspace rail dot、tray、launcher/app badge
```

### 11.2 本轮修复（已确认缺陷）

1. **Mute 下重要未读 Badge**
   - `Sidebar::paintsUnread()`/`redCount()` 不再整体跳过 muted/Nothing：普通 muted 频道未读仍不 bold/不计数；muted 频道的 mention、muted DM 的全部未读仍显示数字和 bold。
   - `SlackBackend::Read::inject()` 对 muted DM 也增加 unread/mentions；muted 频道只保留 mention/DM/关注线程回复。
   - `workspaceAttention()` 不再跳过 muted 会话，而是按 important 规则聚合。
   - 测试：`sidebar: muted rows keep important badges; plain muted unread stays quiet`、`sidebar: sort... muted mentions included`、`slack realtime: a muted DM still counts its unread badge`。

2. **`client.counts` 部分已读 / 游标前进后的状态回退**
   - `mapjson::toCounts()` 保持 channel `has_unreads` 为布尔提示、`mention_count` 为精确值、IM `dm_count` 为精确值。
   - `applyActivity()` 不再用 `max` 永久保留旧值：每个新快照重新建立服务器基数；频道 unread 只承诺“至少一个”，mention/DM 数采用精确基数；快照 `latest` 之后到达的本地消息作为尾部增量补入；本地已读的已加载消息从基数中扣除；本地游标覆盖整个快照时零化该范围。
   - `client.counts` 单飞（`countsInFlight`），避免两个快照响应乱序；`activity` 仍拒绝 `lastRead/latest` 回退的迟到快照。
   - 测试：`slack read: a partial remote read lowers exact mentions and unread`、`slack read: an older counts answer cannot undo a newer partial read`。
   - 已知限制：没有服务器版本号时，无法把“另一个客户端 Mark unread 导致的合法 last_read 回退”和迟到旧响应完全区分；当前优先保护本客户端 cursor，远端回退依赖实时 `*_marked` 事件。见 11.5 backlog。

3. **DND 对桌面通知的抑制**
   - `shell_notify.cpp::notificationsPaused()` 读取自己的 `User::dnd`；`maybeNotify()`、`huddleChanged()`、`notifyReminderDue()` 在其为 true 时停止投递（banner 与 sound 都在 `postNotification()` 内，因而一起停止），但不清除 unread/mention/Threads/Badge。
   - 测试：`notifications: DND pauses delivery but not unread state`、`huddles: ...` 中新增 DND huddle 段。

4. **Thread 自动已读的焦点/阅读语义**
   - `MessageList::checkEdges()` 的 thread 分支增加 `_reading` 和“最新回复实际可见”条件；窗口失焦/隐藏时打开线程或收到新回复不推进 cursor；回到前台且仍在底部时才 markThreadRead。
   - `Shell::updateReading()` 同时更新主消息列表与 ThreadArea 的线程列表；`ThreadArea::setReading()` 转发给 thread MessageList。
   - 手动 Mark unread 的 `Context::unreadHeld` 仍优先于自动已读。
   - 测试：`thread: an unfocused thread does not read new replies; focus does`。

5. **Thread unread 对 Workspace/App Badge 的贡献**
   - `Store` 新增按 conversation 的 `unreadThreadCounts`（`setUnreadThreadCounts()` / `unreadThreads(ConvRef)`）；全局 `unreadThreads()` 仍为总和。
   - `publishUnreadThreads()`（Slack 与 Fake backend）按 conversation 汇总；`workspaceAttention()` 对每个会话使用 `max(DM/mention 计数, 未读关注线程数)`，避免同一回复在 conversation 与 thread 两条链路重复相加。
   - 仅因 following 而重要的非广播 thread reply 不再增加 conversation unread/mentions；显式 mention、DM、broadcast 仍计入 conversation，Threads 计数独立保留。muted 只影响普通高亮，不清除 Threads/attention。
   - 测试：`attention: thread unread joins the badge without double counting`、现有 `sidebar: the Threads entry is bright...`。
   - 这是保守并集，不是精确 Slack 数字：同一会话同时有独立 mention 和独立 thread 时可能低估（见 11.5）。

### 11.3 A–H Checklist 状态与证据

状态说明：Pass = 本轮已验证/修复且有测试或代码路径；Partial = 主路径通过但存在明确限制；Not covered = 本轮未实现/未验证，进入 backlog；N/A = 当前不适用。

#### A. 消息事件与服务端同步

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| A1 | 实时、历史、分页、重连区分 | Pass | `slack_realtime.cpp` 的 `message` 路径经 `deliver/inject`；`pollConversation()` 的 `priming`、`Change::live`；`loadHistory/addPage` 不触发 notification。测试：`core_slack_realtime` 的 socket/message、`core_slack_read` 的 history/pagination/reconnect 用例。 |
| A2 | `client.counts` 初次与轮询 | Pass | `Read::pollUnreadCounts()`、`applyActivity()`；新增单飞与 epoch。测试：`slack read: ...partial remote read...`。 |
| A3 | `has_unreads` / `mention_count` / `dm_count` 精度 | Pass | `slack_json.cpp::toCounts()` 注释与映射；`applyActivity()` 对 channel 用 0/1、对 mention/DM 用精确基数。测试：同一 partial 用例。 |
| A4 | `last_read` 前进/后退/缺失/旧响应 | Partial | 前进、缺失、迟到旧响应有保护（`stale`、`unreadMarks`）；远端合法回退依赖 `*_marked` 实时事件，纯 counts 回退不保证。 |
| A5 | `channel_marked` / `group_marked` / `im_marked` | Pass | `slack_realtime.cpp` 分支直接更新 cursor/count；`b.releaseUnreadMark()` 防止旧 echo 清除本地 Mark unread。测试：`core_slack_realtime` 的 “edits, deletions, reactions, presence, users and marks”。 |
| A6 | Roster / history / counts 竞态 | Pass | `pollRevision`/`completedPoll`、counts 单飞、`activity` baseline；`core_slack_read` 的 stale/partial 用例。 |
| A7 | 跨客户端部分/全部已读、随后新消息收敛 | Pass | 新 partial 用例覆盖 3→1、全部清零；`inject` 实时增量。 |
| A8 | Reconnect / 缓存恢复 / Workspace 切换 | Pass | `resyncUnreads()`、`backfillOpen()`、cache 恢复路径；`core_slack_realtime` 的 reconnect 与 `core_slack_cache`。 |

#### B. Conversation Unread

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| B1 | 新消息增加 unread；自己的消息排除 | Pass | `Store::addMessage()` 的 `author != me`；`Read::inject()` 的 `own`；测试 `sidebar: unread is bold...`、realtime message 用例。 |
| B2 | Mention / channel / @here / @everyone / usergroup 计数 | Partial | `Store::mentionsMe()` 覆盖 `<@me>`、`<!here/channel/everyone>`、`<!subteam^S…>`；mention_count 精确。关键词通知未实现。 |
| B3 | Read cursor 前进减少 unread/mentions | Pass | `Store::markRead()`/`recountUnread()`；新 `applyActivity` 基数重算；测试 partial 用例。 |
| B4 | Mark as Read / Mark as Unread | Pass | `SlackBackend::markRead/markUnread`、`Store::markUnread`；UI `MessageList::markUnread`。测试 `actions: Mark unread...`。 |
| B5 | 手动 Mark unread 不被自动已读/旧响应清除 | Pass | `Context::unreadHeld` + backend `unreadMarks`、`conversation.mark` 确认 epoch；新测试 `an old counts answer cannot clear a local Mark unread`。 |
| B6 | 删除/编辑/广播变化不误改 unread | Pass | `Store::removeMessage/updateMessage`、`reconcileMissingBroadcasts()`；realtime broadcast 删除用例。 |
| B7 | Mute 与 All/Mentions/Default 语义 | Pass | `Conversation::effectiveNotify()`、`Sidebar::level()`、`maybeNotify()`；菜单测试。 |
| B8 | 静音会话重要 Badge 保留、普通消息抑制 | Pass | 本轮修复；`sidebar: muted rows keep important badges...`、`slack realtime: a muted DM...`。 |

#### C. Thread Unread / Follow

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| C1 | 自己发起/参与/被提及/手动关注正确 Follow | Pass | `Read::inject()` 的 follow/unfollow、`follow()`/`isFollowed()`；`core_slack_features` 与 `core_slack_realtime` thread 用例。 |
| C2 | Unfollow 后旧 Feed 不恢复 Follow | Pass | `unfollowed` map、`feedOwns()`、`setFollowLocal()/followSettled()`；features 的 follow tests。 |
| C3 | Conversation mute / Thread mute / Follow 独立 | Pass | `threadMuted` 是独立 Mark；follow 状态独立；`publishUnreadThreads()` 只排除 thread mute。 |
| C4 | Thread read cursor 独立于 Conversation | Pass | `threadReadFloor/threadBaseline`；测试 `notifications: channel lastRead filters live posts, not independent thread replies`。 |
| C5 | Thread 新回复/批量/广播计数 | Pass | `noteUnreadThreadReply()`、`announceReplies()`、broadcast inject；realtime broadcast 用例。 |
| C6 | 跨客户端 Thread 已读同步 | Pass | `feedThread()` 的 `lastRead`/`threadRead()`；features 的 feed/read tests。 |
| C7 | 手动 Mark Thread unread pending/epoch | Pass | `threadUnreadPending`、`threadUnreadMarkEpoch`、`threadMarkConfirmed()`；features 的 old-feed/guard tests。 |
| C8 | 窗口失焦/最小化/隐藏时 Thread 自动已读 | Pass | 本轮 `_reading` + visible 条件；`Shell::updateReading()`；测试 `thread: an unfocused thread...`。 |
| C9 | Thread 打开期间新回复自动已读 | Pass | 同上；失焦时保持 unread，回焦且可见才推进。 |
| C10 | Thread-only unread 进入 Threads/Workspace/Tray/App | Pass（保守口径） | `Store::unreadThreadCounts`、`workspaceAttention()` 的 per-conv max；测试 `attention: thread unread...`。 |

#### D. Activity

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| D1 | Mention/DM/Thread/Reaction/Channel item 解析分类 | Pass | `slack_json.cpp::toActivityItem()`/`activityUnread()`；`activity page: ...tabs/filter` 测试。 |
| D2 | Feed 初次/分页/刷新/workspace switch generation | Pass | `activity_page.cpp` 的单飞/快照保留；测试 `activity page: ...`, `overview: Activity retains last good snapshot...`。 |
| D3 | Unread-only 过滤 | Pass | `ActivityPage::setUnreadOnly()`；`activity page: mark as read / unread...`。 |
| D4 | 单项 Mark Read/Unread 即时更新 row/全局计数 | Pass | `ActivityPage::toggleRead()`/`Backend::setActivityRead()`；同上测试。 |
| D5 | 打开 Activity item 推进对应 Conversation/Thread cursor | Pass | `activity page: an item opens its message or its thread and is read`。 |
| D6 | Activity Mark Read 不清除其他通知/Conversation unread | Pass（代码限定） | Activity 的 mark 只作用于 item；Conversation cursor 由打开动作单独推进。未新增专门负向测试。 |
| D7 | Activity count 与 Feed item 数量口径 | Pass | `mapjson::activityUnread()` 与 `Store::unreadActivity()`；`activity page: entry is bright...`。 |
| D8 | 未支持 Activity 类别/偏好 | Not covered | 见 11.5 backlog。 |

#### E. Desktop Notification

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| E1 | 历史/重连/缓存恢复不产生通知 | Pass | `Change::live`/`isArrival`；`notifications: history and cached thread pages are silent...`。 |
| E2 | 实时 Append/Insert/Arrived 仅真实新消息 | Pass | `Shell::messagesArrived()`；上述测试 + out-of-order insert。 |
| E3 | 当前窗口/会话正在阅读时抑制 | Pass | `maybeNotify()` 的 `reading() && conv == _current`；`sidebar: the open chat isn't read...` 与通知测试。 |
| E4 | 全局 Everything / Mentions and DMs | Pass | `Settings::notifyLevel`/`defaultLevel()`；`notifications: the titles and gates...`。 |
| E5 | Conversation All / Mentions / Mute override | Pass | `effectiveLevel()`；同一测试的 muted/mentions 分支。 |
| E6 | Mention/keyword/DM/Followed Thread 重要性 | Partial | mention/DM/followed thread 已实现；keyword 未实现。`maybeNotify()` important 判断。 |
| E7 | Workspace mute / Conversation mute / Thread mute | Pass | `workspaceMuted`、`effectiveLevel`、`threadMuted` 分支；workspace 与 conversation 测试，thread mute 在消息菜单测试。 |
| E8 | DND / Pause / Schedule | Partial | DND 本轮修复并测试；schedule/时区尚无本地设置，仅依赖 Slack 事件的 `dnd` 状态。 |
| E9 | Huddle / Reminder 各自开关与 DND | Pass（DND 主路径） | `huddleChanged()`/`notifyReminderDue()` 加 DND gate；huddle DND 测试。Reminder DND 无独立 UI 测试。 |
| E10 | 重复事件去重、已读后撤回 | Pass | `notifications: read conversations and threads take theirs back...`；`withdrawRead()`。 |
| E11 | 点击通知打开会话/线程 | Pass | 通知点击测试、thread click 测试。 |

#### F. Badge / Highlight / Attention

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| F1 | Conversation bold / unreads-only | Pass | `Sidebar::paintsUnread()`、filters；`sections: collapsed groups...` 等。 |
| F2 | 红色数字与蓝点区别/计数口径 | Pass | `redCount()` + `rowState.dot`；`sidebar: unread is bold...`。 |
| F3 | 静音 Conversation 的 mention/DM Badge | Pass | 本轮修复；`sidebar: muted rows...`。 |
| F4 | Threads / Activity 导航 unread | Pass | `refreshNav()` 使用 `unreadThreads()/unreadActivity()`；`sidebar: the Threads entry...`、Activity 测试。 |
| F5 | Workspace Rail 点/数字 | Pass | `Shell::updateAttention()`、`RailTile::setDot`；`workspaces: each tile shows its own dot...`。 |
| F6 | Tray/Launcher/App icon 数字和点 | Pass | `setBadgeCount()`/tray；`workspaces: ...badge sums...`。 |
| F7 | 多 Workspace 汇总、mute、切换 | Pass | `updateAttention()` 跳过 `workspaceMuted`；workspaces 测试。 |
| F8 | Thread-only unread 计入与去重 | Pass（保守） | `workspaceAttention()` 的 per-conv `max`；测试 `attention: thread unread...`。已知独立 mention + 独立 thread 可能低估。 |
| F9 | 离开/关闭/过期 unread 清理 | Pass | `workspaceAttention()` 跳过非 member 和 30 天窗口；测试 `attention: ... Idle past...`。 |

#### G. 自动 Mark as Read

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| G1 | 首次打开初始定位 | Pass | `MessageList::applyOpenTarget()`；`messages_opening` 测试。 |
| G2 | Start where left off / newest / leave unseen | Not covered | 仅“从上次位置/首个未读”行为；三种 Slack preference 未实现，backlog。 |
| G3 | 滚动到底部/最新真实可见 | Pass | Conversation `_list->lastVisible()`；thread 本轮增加 visible 条件。 |
| G4 | 窗口焦点/隐藏/最小化 | Pass | `Shell::reading()`/`updateReading()`；测试 focus/hide/minimize。 |
| G5 | 重新获得焦点推进 cursor | Pass | FocusIn → `setReading(true)` → `checkEdges()`；测试。 |
| G6 | Thread 打开/新回复/失焦规则 | Pass | 本轮修复 + 测试。 |
| G7 | Activity/Threads 页面跳转后的已读 | Pass | `ThreadsPage::Card::openThread()`/Activity activate；页面测试。 |
| G8 | 手动 Mark unread 保护期限/解除 | Pass | `Context::unreadHeld` + backend epoch；UI/后端测试。 |
| G9 | 自动已读、服务端 cursor、本地 Store 最终一致 | Pass（有限制） | `markRead` 写 `conversations.mark`；counts 合并 + 单飞；remote rollback 见 11.5。 |

#### H. 设置与持久化

| # | 项目 | 状态 | 代码/测试证据与说明 |
|---|---|---|---|
| H1 | 全局通知级别与桌面开关 | Pass | `Settings::notifyLevel/notifications`；settings/shell 测试。 |
| H2 | Conversation Default/All/Mentions/Mute 优先级 | Pass | `effectiveNotify()`；`menus: conversation actions...`、`sidebar: ...Default...`。 |
| H3 | Muted channels Slack 同步/多客户端 | Pass | `slack_actions.cpp` mute read-modify-write；`core_slack_features` mute tests。 |
| H4 | Thread Follow/Unfollow Slack 同步/缓存 | Pass | `subscriptions.thread.add/remove`、`followHeld`；features tests。 |
| H5 | Thread mute 本地持久化 | Pass | `Store::_mutedThreads`、workspace cache；message menu 测试。 |
| H6 | DND 状态刷新、schedule、时区 | Partial | RTM `dnd_updated` + `/dnd` 更新；schedule/时区配置未实现，backlog。 |
| H7 | 设置更改后通知/UI 即时刷新 | Pass | `settings` 保存/`usersChanged`/`Meta` 观察者；相关 UI 测试。 |
| H8 | 不支持配置明确 backlog | Pass | 本文件 11.5 列出。 |

### 11.4 Slack 规则与 MSGA 行为差异表

| Slack 规则 | 本轮前 MSGA | 本轮后 | 状态/后续 |
|---|---|---|---|
| Muted 频道被 mention 仍有数字 Badge | muted 直接清零 | 保留 mention 数字，普通 muted unread 仍不 bold | 已对齐 |
| Muted DM 新消息仍有 Badge | muted DM 不增加 unread | 增加 unread/mentions | 已对齐 |
| Muted 会话中有人回复我发的消息，Threads 加粗 | thread 计数独立但 badge 未汇总 | Threads 计数进入 per-conv attention | 已对齐（保守） |
| DND/Pause 抑制 banner/sound，不清 unread | `maybeNotify` 未检查自己 DND | gate 加入消息/huddle/reminder | 已对齐 |
| Thread 阅读 cursor 与频道独立 | 已独立 | 保持并加焦点/可见条件 | 已对齐 |
| 打开但失焦的 Thread 不应吞新回复 | 无条件自动已读 | `_reading` + 最新可见才推进 | 已对齐（产品决策） |
| App icon Badge 包含关注 Thread 回复 | workspaceAttention 不含 thread | per-conv max 并入 | 已对齐（保守口径） |
| `has_unreads` 是布尔、`mention_count`/`dm_count` 精确 | 首次 max，后续不降 | 每次快照重建基数 + 本地尾部增量 | 已对齐 |
| 另一客户端部分读掉 mention 后 Badge 下降 | max 永久保留 | 精确 mention 基数下降 | 已对齐 |
| 三种 Mark as Read preference | 单一“从上次位置/未读”行为 | 未增加 | Feature gap/backlog |
| Channel keywords | 未实现 | 未实现 | backlog |
| DND schedule / 时区 / 自定义星期 | 仅有事件驱动 DND | 未增加配置 | backlog |
| Conversation Desktop/Mobile notification override | 仅本地 NotifyLevel | 未增加 | backlog |
| Badge every unread message | 未提供开关 | 未增加 | backlog |
| Follow every thread（频道级） | 仅单线程 Follow | 未增加 | backlog |
| Activity 类型显示偏好/更多类别 | 固定 All/DMs/Mentions/Threads | 未扩展 | backlog |
| VIP 在 DND/focus 下的例外 | VIP 仅用于 sidebar 分区 | 未增加投递例外 | backlog |
| Activity clear 与 conversation cursor 的完整语义 | item 级 mark read | 未改 | 未发现需本轮修复的确定性缺陷；继续审计 |
| Reminder 在 DND 下 | 只检查全局开关 | 加入 DND gate | 已对齐（按“所有通知暂停”语义） |

### 11.5 关键决策记录

#### D1. Thread 在窗口失焦/隐藏时是否自动已读

- **问题/证据**：`MessageList::checkEdges()` 的 thread 分支原先只检查加载、pending、`unreadHeld`，没有 `_reading`；源码注释写“on load and on each new reply”。Conversation 分支已有 `_reading` + 最新可见条件。
- **Slack 规则**：Threads 有独立 cursor；官方 Mark as Read 规则说明“打开频道后不久标记已读”，但没有说明失焦窗口；产品上后台窗口不应替用户阅读。
- **选项**：A 仅聚焦且最新可见时自动读（回焦后补读）；B 保持当前总是读；C 初次打开读一次、后台回复不读。
- **Subagent（`openai/gpt-6.1-sol#max`）结论**：选 A；初次加载时已失焦/隐藏不标已读；回焦且仍在底部才推进；手动 Mark unread 优先。
- **主 Agent 判断/落地**：采纳 A；增加 `_reading` + `lastVisible()`，并让 Shell 把阅读状态传给 ThreadArea。Threads 页面卡片点击仍立即 `markThreadRead()`（用户显式点击，不是后台自动行为）；若未来要求“点击但未滚到底”也严格按可见性，可再收敛。
- **验收**：`thread: an unfocused thread does not read new replies; focus does`。

#### D2. 服务器 counts 与本地消息如何合并

- **问题/证据**：`toCounts()` 的 channel `has_unreads` 只有 1/0，`mention_count` 精确；IM `dm_count` 精确。旧 `applyActivity()` 用 max 保留本地值，部分已读后不下降；旧异步响应可能覆盖本地 cursor。
- **选项**：A 每次用快照基数 + 快照 `latest` 后本地增量 + 本地已读扣除；B 继续 max；C 完全从本地已加载消息重算。
- **Subagent 结论**：A 的方向正确；必须让新快照替换旧服务器基数，不能把时间戳当版本；串行化 counts，按操作 epoch 保护 Mark unread；部分历史不完整时不要把局部扣减宣称为精确；频道 unread 只承诺布尔。
- **主 Agent 判断/落地**：采用 A 的保守实现：counts 单飞；`stale` 拒绝 cursor/latest 回退的迟到快照；`readThrough = max(local, server)`；channel 基数 0/1、mention/DM 精确；加载消息扣减已读、补 `latest` 后增量；`unreadMarks` + write confirmation epoch 保护本地 Mark unread。没有把 max 作为永久底数。
- **限制**：纯 counts 无法可靠区分“远端合法 Mark unread 回退”与旧快照；当前依赖 `channel_marked` 实时事件；见 backlog。
- **验收**：partial/old-response/local-mark-unread 三个新测试。

#### D3. Thread-only unread 对 Workspace/App Badge 的计数口径

- **问题/证据**：`Store::unreadThreads()` 只有全局总数；`workspaceAttention()` 只聚合 conversation unread/mentions；`inject()` 对 followed thread reply 同时增加 conversation mentions 和 thread 集合，直接相加会重复。
- **选项**：A 直接加全局 thread 数（重复）；B 按会话 `max(conversation important, thread count)`；C 完整 per-thread 归属/精确交集；D 不加入（backlog）。
- **Subagent 结论**：选 B 的改良版：per-conversation `T`，`Attention.important += max(C,T)`；仅 following 的非广播回复不再进入 C；mute/DND 不清 thread；接受“独立 mention + 独立 thread 同会话可能低估”的保守下界。
- **主 Agent 判断/落地**：采纳；新增 `Store::unreadThreadCounts`，Slack/Fake `publishUnreadThreads()` 维护，`workspaceAttention()` 逐会话 max；`inject()` 对 following-only 非广播回复不再加 conversation unread/mentions。没有引入频道行线程数字，不扩大 UI 范围。
- **验收**：`attention: thread unread joins the badge without double counting`。

#### D4. 分层边界

- **判断**：不引入大型 `NotificationPolicy` 重构。保留 `Store` 为 unread/cursor 数据源；`Shell::maybeNotify` 为投递策略；`Sidebar/workspaceAttention` 为展示策略。DND/Mute 只影响策略输出，不直接改 Store。
- **理由**：本轮缺陷都能在现有边界内修复；统一策略对象会扩大风险且没有测试收益。

### 11.6 Podman 测试命令与结果

镜像与构建：

``` sh
podman run --rm --security-opt label=disable \
  -v /home/zce/Coding/msga:/workspace:rw \
  -v /tmp/opencode/msga-build:/build:rw \
  -w /workspace localhost/msga-pr41-quality:20261108 \
  bash -lc 'cmake -S /workspace -B /build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DMSGA_BUILD_TESTS=ON -DMSGA_DEMO=OFF -DMSGA_SELF_UPDATE=OFF \
    -DFETCHCONTENT_SOURCE_DIR_MSGA_MBEDTLS=/workspace/build/_deps/msga_mbedtls-src'

podman run --rm --security-opt label=disable \
  -v /home/zce/Coding/msga:/workspace:rw \
  -v /tmp/opencode/msga-build:/build:rw \
  -w /workspace localhost/msga-pr41-quality:20261108 \
  bash -lc 'cmake --build /build --parallel 8 \
    --target msga_base_tests msga_net_tests gfx_tests text_tests ui_tests msga_app_tests'
```

针对性测试（本轮修复）：

``` sh
ctest --test-dir /build --output-on-failure \
  -R "shell_tests|core_slack_read|core_slack_realtime" --parallel 3
# 100% tests passed, 0 failed (shell_tests, core_slack_read, core_slack_realtime)
```

Slack/消息/Shell 相关回归：

``` sh
ctest --test-dir /build --output-on-failure \
  -R "core_slack|core_store|core_fixture|messages_thread|messages_actions|messages_opening|shell_ui_tests|shell_tests" \
  --parallel 4
# 15/15 passed
```

完整测试（含 base/net/gfx/text/ui/app）：

``` sh
ctest --test-dir /build --output-on-failure
# 121/121 tests passed, 0 failed
# Total Test time (real) = 263.85 sec
```

说明：完整运行使用同一容器 / 同一构建目录；`core_slack_read` 约 64 s。ctest 的 `fork_bundle_metadata` / `dependency_license_inventory` 也在 121 项内通过。一次 `--parallel 4` 运行中 `core_slack_cache` 因 3 s 的 `users.prefs.get` 等待在负载下超时（非本轮代码路径）；该用例单独运行通过，随后完整串行运行 121/121 通过。建议 CI 保留现有的并行度，但该用例的超时在慢机器上偏紧，属于既有测试稳定性 backlog。

### 11.7 剩余风险与 backlog（按优先级）

1. **P1 远端 `Mark unread` 的 counts 回退**：纯 `client.counts` 无版本号，当前 `stale`/max 保护优先防旧响应；另一客户端把 cursor 回退且没有实时 `*_marked` 事件时，MSGA 可能继续显示已读。建议后续用 Slack 的 `channel_marked` 事件/操作世代或服务端游标快照时间解决。
2. **P1 三种 Mark as Read preference**：当前只有“从上次位置/首个未读 + 最新可见即读”。Start newest / leave unseen unread 未实现；需要设置项、打开定位和自动已读策略联动。
3. **P2 DND schedule / 时区 / 星期**：当前只消费 Slack 事件的 `dnd`，没有本地 schedule 配置；schedule 结束依赖事件，可能滞后。需要设置模型和时区测试。
4. **P2 Channel keywords**：`mentionsMe` 不包含关键词；通知/高亮/Activity 均未实现。依赖关键词设置与完整词匹配规则。
5. **P2 Follow every thread / Conversation Desktop/Mobile override / Badge every unread**：设置能力缺失，非本轮必要正确性修复。
6. **P2 Activity 类型偏好与更多类别**：当前固定四类视图；未实现 Slack 的 Activity 类型过滤/自定义视图。
7. **P2 Thread attention 精确交集**：per-conv max 是保守并集；同一会话独立 mention + 独立 thread 可能低估。要精确需要知道服务器 mention/DM 基数是否已包含线程回复，或按 thread root 去重。
8. **P3 Reminder DND 产品语义**：本轮按“所有通知暂停”抑制 reminder banner；若产品希望 reminder 穿透 DND，应改为明确设置。
9. **P3 频道行 Thread 数字/高亮**：本轮只保证 Threads 入口、Workspace rail、tray/app badge；未给频道行增加线程数字，避免扩大 UI 范围。

### 11.8 未修改/未验证项

- 未修改 Activity 的 item 语义、分页/过滤实现；仅用现有测试回归。
- 未修改通知点击、持久化、Huddle 房间解析、workspace 切换实现。
- 未在宿主机执行任何编译/测试；未提交、推送、合并或发布。
- 未覆盖真实 Slack 服务端；所有行为基于 fake Slack/Store/Shell 测试和官方文档规则。
