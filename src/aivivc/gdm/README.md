# AIVIVC GDM 桥接

本目录包含 AIVIVC 面向 GDM 的桥接实现。

- `aivivcgdmconfig`：模拟 SOS 风格的 GDM 配置探测命令。
- `aivivc-gdm`：接收常见 GDM 命令参数，并分发到 Git/SVN。
- `aivivc_gdm_wrap.c`：构建 Cadence 原生 GDM 封装库，产物为 `libgdmaivivc_sh.so`。
- `displayPrefs`：把 AIVIVC/SOS 兼容状态名映射到 Library Manager 状态图标。

命令适配器刻意与原生共享库分离。Library Manager SKILL 回调和
`libgdmaivivc_sh.so` 都使用同一个适配器路径，并通过
`AIVIVC_GDM_ADAPTER` 控制。

当前适配器支持 `status`、`status-code`、`history`、`ci`、`co`、
`update`、`cancel`、`delete`、`protect`，参数支持 `-file`、`-lib
lib.cell:view/file`、`-cdslib`、`-recurse`、`-version` 以及提交信息参数。

`co` 会记录 AIVIVC checkout 标记，并把受控文件设为可写。`ci`、
`cancel` 和 `update` 会清理或保留标记，并把受控文件重新保护为只读。
Git 标记位于 `.git/aivivc`；SVN/回退标记位于 `.aivivc`。
标记所有权对 checkout 路径及其子路径生效，因此 checkout 一个 view/目录时，
其下受控 OA 文件也会报告为已 checkout。`ci` 和 `cancel` 会释放与目标路径重叠的
当前用户标记，包括父级 view/目录标记。`protect` 可识别标记：对父目录或工作区
根目录执行时，它会保留当前用户已 checkout 路径的写权限，并保护其余文件。

直接冒烟测试：

```sh
src/aivivc/gdm/aivivcgdmconfig
src/aivivc/gdm/aivivc-gdm status -file .
src/aivivc/gdm/aivivc-gdm status-code -file .
src/aivivc/gdm/aivivc-gdm history -lib myLib.myCell:schematic/master.tag
```

回归测试：

```sh
bash regtest/test_aivivc_gdm_adapter.sh
```

Cadence 侧预期环境：

```sh
export GDM_USE_SHLIB_ENVVAR=yes
export CDS_GDM_SHLIB_LOCATION=/workarea/ai/openclaw/oaplg/lib/linux_rhel90_64/opt
export AIVIVC_GDM_ADAPTER=/workarea/ai/openclaw/oaplg/lib/linux_rhel90_64/opt/aivivc-gdm
```

受控工作区或库层级还需要：

```text
DMTYPE aivivc
```

原生封装库说明：

- Cadence `libgdmimppi_sh.so` 包含 `gdmLoadSharedLib`、`gdmLookupSymbol`，
  并会查找 `gdm%sWrapFuncs`。
- SOS 导出 `gdmsosWrapFuncs`，因此 AIVIVC 需要从 `libgdmaivivc_sh.so`
  导出 `gdmaivivcWrapFuncs`。
- 原生封装库使用 SOS 兼容的 30 槽函数表。它把 GDM 操作映射到
  `aivivc-gdm`，并实现按文件的 checkout、modified、permission 和
  update-needed 状态槽，使 Library Manager 能选择对应状态图标。
- Cadence 低层 checkout 状态使用 `CO` 表示当前用户 checkout，使用 `COE`
  表示其它用户 checkout。已 check-in 时返回 `NULL`，GDM 会报告为
  `gdmStateCI`。
- 权限状态使用 `READ`/`WRITE`；当前用户 checkout 且文件可写时报告
  `WRITE`。
