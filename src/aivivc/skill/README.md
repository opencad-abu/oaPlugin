# AIVIVC Virtuoso Library Manager 集成

本目录刻意把 CIW 可安全加载的回调与 Library Manager 菜单定制分开。

- `aivivcCallbacks.il`：AIVIVC 动作回调和命令桥。可在 CIW 或
  Library Manager 上下文中加载。它也定义 Library Manager 菜单的
  `mapCallback`，因为 pre-map 求值可能发生在与菜单创建不同的 SKILL 上下文。
- `aivivcLibMgr.il`：Library Manager 定制文件。它会在 Library
  Manager 上下文内加载 `aivivcCallbacks.il`，然后用 `lmgrCreateMenu`
  等 API 创建菜单。
- `cdsLibMgr.il`：指向 `aivivcLibMgr.il` 的符号链接，用于 Cadence Library
  Manager 定制加载。

## 加载方式

启动 Virtuoso 前设置 SKILL 目录：

```sh
export AIVIVC_SKILL_DIR=/workarea/ai/openclaw/oaplg/src/aivivc/skill
```

CIW 的 `.cdsinit` 可以只加载回调：

```skill
load("/workarea/ai/openclaw/oaplg/src/aivivc/skill/aivivcCallbacks.il")
```

不要把菜单定制文件作为唯一 CIW 入口加载，也不要在 CIW 里直接调用
`lmgrCreateMenu` 或 `lmgrDefineInits`。这些 API 属于 Library Manager
定制上下文。

让 Library Manager 加载：

```skill
load("/workarea/ai/openclaw/oaplg/src/aivivc/skill/cdsLibMgr.il")
```

也可以配置 Library Manager 定制路径，使其找到 `cdsLibMgr.il`。
当它在 Library Manager 上下文中加载时，`aivivcLibMgr.il` 会在同一上下文
中再次加载 `aivivcCallbacks.il`，从而让菜单项回调和菜单 pre-map 回调
都能看到相应符号。

如果 CIW 能访问 `lmgrDefineInits`，`aivivcLibMgr.il` 会注册一个加载
`cdsLibMgr.il` 的表达式，而不是裸函数名。这一点很重要，因为 Library Manager
会在独立 SKILL 上下文中求值 init 回调。

如果使用 SVN，确保 Virtuoso 继承：

```sh
export PATH=/software/pkgs/subversion/usr/bin:$PATH
export LD_LIBRARY_PATH=/software/pkgs/subversion/usr/lib64:${LD_LIBRARY_PATH:-}
```

## 菜单行为

默认情况下，脚本会隐藏 Library Manager 内置的 `designCascade` 菜单项，并新增一个
标签为 `Design Manager` 的替代菜单。如需保留原菜单并新增独立的 `AIVIVC` 菜单：

```sh
export AIVIVC_REPLACE_DM_MENU=0
```

可用动作：

- `Status...`
- `Version Info...`
- `History...`
- `Update`
- `Make Editable`
- `Commit...`
- `Cancel Edit...`

`Commit...` 设置了 `AIVIVC_COMMIT_MSG` 时使用该提交信息，否则使用默认提交信息。

状态改变动作调用 `AIVIVC_GDM_ADAPTER`：

- `Make Editable` -> `aivivc-gdm co`
- `Commit...` -> `aivivc-gdm ci`
- `Cancel Edit...` -> `aivivc-gdm cancel`
- `Update` -> `aivivc-gdm update`
- `Status...`/`History...` -> `aivivc-gdm status`/`history`

`Version Info...` 是唯一直接执行 Git/SVN 只读查询的动作。

执行 `Make Editable` 后，AIVIVC 写入 checkout 标记，并把受控文件设为可写。
执行 `Commit...` 或 `Cancel Edit...` 后，它会释放标记，并把受控文件重新保护为
只读。无改动提交也会释放 checkout 标记，因此一个干净 Git 工作树可以从 GDM
`CO/WRITE` 回到 `CI/READ`。

对于原生 GDM 集成，确保工作区或 library 中有：

```text
DMTYPE aivivc
```

并让 Virtuoso 继承：

```sh
export GDM_USE_SHLIB_ENVVAR=yes
export CDS_GDM_SHLIB_LOCATION=/workarea/ai/openclaw/oaplg/lib/linux_rhel90_64/opt
export AIVIVC_GDM_ADAPTER=/workarea/ai/openclaw/oaplg/lib/linux_rhel90_64/opt/aivivc-gdm
```
