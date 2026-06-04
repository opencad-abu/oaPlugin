# oaPlugin

oaPlugin 是 [AIVI](https://github.com/opencad-abu/aivi) 的 OpenAccess 插件集合，将版本管控、DM 文件系统、DMTurbo、PCell、文本、锁和库定义接入 OA 平台。

## 插件模块

| 模块 | 说明 |
|------|------|
| aivivc | 版本管控桥梁（Git/SVN + GDM 适配） |
| aividmfilesys | DM 文件系统插件 |
| aividmfilesysbase | DM 文件系统基础库 |
| aividmturbo | DMTurbo 数据管理插件 |
| aividmturbobase | DMTurbo 基础库 |
| aividmturbo_server | DMTurbo 服务端 |
| aivipcell | 参数化单元 (PCell) 插件 |
| aivitext | 文本渲染插件 |
| aivilock | 锁管理插件 |
| aivilibdef | 库定义插件 |

## 依赖

- OpenAccess SDK 22.61+
- GCC 9.3+ (C++17)
- GNU Make

## 构建

```bash
./build_all.sh
```

## 文档

https://opencad-abu.github.io/oaPlugin-docs-zh/

## License

[MIT](LICENSE)
