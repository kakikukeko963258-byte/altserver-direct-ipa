# AltForge Agent Rules

本文件补充全局工程规则；冲突时遵循更严格的安全、正确性、资源和 Git 要求。

## 开始顺序

1. 检查 root 和相关 submodule 的 `git status`。
2. 阅读 `README.md`、`docs/README.md` 和本任务涉及的 workflow/knowledge/rules。
3. 阅读 `Podfile.lock`、workspace `Package.resolved`、`.gitmodules`、相关 scheme/target 和真实调用链。
4. 明确 `FR -> DES -> TEST -> T` 与 change workspace，再实施高影响变更。

## 项目边界

- iOS 主应用在 `AltStore/`，macOS 安装服务在 `AltServer/`，domain/persistence 在 `AltStoreCore/`，跨进程契约在 `Shared/`。
- 签名、Apple API、application archive 属于 `Dependencies/AltSign`。不要在 UI 层重复修复底层问题。
- 当前发布是 Classic 形态；`marketplace` 是历史 branch 名，不代表 release 嵌入 Marketplace extension。
- Windows 服务源码位于 `AltServer-Windows/`；本机或 CI 未实际完成 MSBuild/设备验证时，不得把对应验证写成已通过。
- 对外品牌使用 AltForge；无必要不要批量重命名历史 `AltStore`/`ALT` 类型，以降低上游同步冲突。

## 构建与验证

- Install：`git submodule update --init --recursive`，然后 `pod install --deployment`。
- iOS build：`xcodebuild build -workspace AltStore.xcworkspace -scheme AltStore -configuration Debug -destination "generic/platform=iOS Simulator" CODE_SIGNING_ALLOWED=NO`。
- Tests：使用 `AltStore` scheme/`AltTests` target；CI destination 以 `.github/workflows/ci.yml` 为准。
- macOS build：`xcodebuild build -workspace AltStore.xcworkspace -scheme AltServer -configuration Debug -destination "generic/platform=macOS" CODE_SIGNING_ALLOWED=NO`。
- 运行 build 前检查现有 process；使用有界 timeout/DerivedData，任务后只清理自己创建的资源。
- Signing、provisioning、install 和 JIT 变化不能只靠 simulator；需要脱敏真实设备计划。

## 高风险规则

- Apple ID、密码、2FA、UDID、certificate、private key、profile、Cookie 和 anisette data 永远视为敏感。
- `Shared/Server Protocol` 变化必须验证 client/server 双端。
- Core Data model 变化必须提供 migration 与回滚说明。
- IPA/ZIP 输入不可信；验证 encoding、path、size、失败清理，不允许路径穿越。
- Localization 使用既有 string catalog；新增用户文案同步英文与简体中文或保留明确 fallback。

## Submodule

- Nested repo 改动先在 nested repo 提交并推送，再更新 superproject gitlink。
- `.gitmodules` 必须指向能获取目标 commit 的 remote；AltSign fork 为 `legeling/AltSign`，upstream 为 `rileytestut/AltSign`。
- 不把 dirty submodule 当成已交付修改。

## 文档驱动

- 路由遵循 `spec-init.topology.yml` 与 `docs/rules/document-routing-rules.md`。
- 新功能/bugfix/重构/流程变化创建 active change；完成后 converge 并移动到 completed/legacy。
- 测试矩阵、回归、fixture 和覆盖缺口分别维护在 `docs/workflow/04-verification/` 对应文件。
- `[待确认]` 不得被静默当作已确认决策。
- 完成门禁见 `docs/rules/definition-of-done.md`。

## Git 与交付

- 提交遵循 `docs/rules/commit-rules.md`；标题默认使用 `<type>(<scope>): <summary>`，文档类提交可用 `docs: ...`。
- 脏工作区必须使用显式路径暂存并检查 staged diff；禁止用 `git add -A` 混入其他任务或用户改动。
- 一个提交只承载一个可解释、可回滚的逻辑变化。生成文件仅在项目确实依赖它时与源变更一起提交。
- 提交前运行与风险匹配的验证和 `git diff --cached --check`；未运行项及残余风险必须如实报告。
- 未经用户明确要求，不推送、不建 tag/PR/release、不改写历史。Submodule 提交顺序继续遵循上节约定。

## 规则入口

- 编码、Bug 修复、澄清、Issue、Review、依赖、Localization、安全、发布、资源和归档规则统一从 `docs/rules/README.md` 进入。
- 规则描述默认做法；需求、设计、单次任务和历史讨论必须写入各自文档层，不能塞进规则文件。
- 规则本身发生变化时，建立 change 记录并同步 `AGENTS.md`、相关 workflow/knowledge 和规则索引。
