[English](README.md)

# fv

一个用于检查 Ansys Fluent `.cas.h5` / `.msh.h5` 文件的命令行工具，**无需打开 Fluent**。

- 直接从 HDF5 文件中读取求解器设置、材料、边界条件、离散格式等信息。
- 使用 [PyVista](https://pyvista.org) 可视化网格。
- 网格读取器基于 VTK 的 [`vtkFLUENTCFFReader`](https://github.com/Kitware/VTK/tree/master/IO/FLUENTCFF) 修改实现，移除了 VTK 依赖。支持所有 VTK 单元类型（包括多面体单元），兼容 `.cas.h5` 和 `.msh.h5`。

---

## 安装

```bash
pip install fv
```

### 从源码编译

需要 HDF5 开发头文件和库。构建脚本会按以下顺序自动检测：`HDF5_DIR` 环境变量 → 本地 `include/lib` → `pkg-config` → conda → Homebrew（macOS）→ 常见系统路径。

```bash
git clone https://github.com/preamer/fv.git
cd fv
pip install .
```

如果自动检测失败，请手动设置 `HDF5_DIR` 后重新安装。

---

## 用法

```
fv <文件> [选项]
```

### 选项

| 选项 | 说明 |
|---|---|
| `-v`, `--version` | 打印文件中存储的 Fluent 版本号 |
| `-t <版本>`, `--to <版本>` | 修改文件中存储的版本号，例如 `25.2` |
| `--extract` | 将原始 Scheme 设置导出到 `general.scm` 和 `boundary.scm` |
| `--showmesh` | 使用 PyVista 交互式显示网格 |
| `--solver` | 求解器类型、时间类型、维度、精度、湍流模型、能量方程、辐射模型、重力 |
| `--mat` | 材料属性 |
| `--bd` | 边界条件设置 |
| `--ne` | 命名表达式 |
| `--disc` | 离散格式和松弛因子 |
| `--rd` | 报告定义 |
| `--plotsets` | 图表集配置 |
| `--monitorsets` | 监控集配置 |
| `--iter` | 迭代步数 / 时间步设置 |
| `--save` | 将输出保存为 `<文件>.json` |

多个选项可以自由组合。算例设置类选项（`--solver`、`--mat` 等）仅适用于 `.cas.h5` 文件。

### 示例

```bash
# 查看求解器配置和边界条件
fv case.cas.h5 --solver --bd

# 查看所有设置并保存为 JSON
fv case.cas.h5 --solver --mat --bd --ne --disc --rd --iter --save

# 可视化网格（.cas.h5 和 .msh.h5 均支持）
fv case.cas.h5 --showmesh
fv mesh.msh.h5 --showmesh

# 查看文件中存储的 Fluent 版本
fv case.cas.h5 --version

# 修改版本号（用于跨版本兼容）
fv case.cas.h5 --to 25.2

# 导出原始 Scheme 字符串以便手动查看
fv case.cas.h5 --extract
```

---

## 文件格式支持

| 功能 | `.cas.h5` | `.msh.h5` |
|---|---|---|
| 算例设置（`--solver`、`--mat`、`--bd` 等） | ✅ | — |
| 网格可视化（3D） | ✅ | ✅ |
| 网格可视化（2D） | ✅ | ⚠️ 部分支持 |

> **注：** 2D `.msh.h5` 文件的面连通性（C0/C1）解析尚未完全实现。

---

## 支持的设置项

| 选项 | 内容 |
|---|---|
| `--solver` | 算法（PBNS/DBNS）、稳态/瞬态、2D/3D、单/双精度、湍流模型（层流 · k-ε · k-ω · SA · RSM · LES · DES …）、能量方程、辐射模型、重力 |
| `--mat` | 流体/固体材料的属性及其求值方式 |
| `--bd` | 速度入口、压力出口、质量流量入口/出口、壁面（热边界条件和运动边界条件）、多孔跳跃、内部、对称面等 |
| `--ne` | 算例中定义的命名表达式 |
| `--disc` | 各方程的离散格式（二阶迎风、QUICK 等）及松弛因子/伪瞬态因子 |
| `--rd` | 报告定义（体积、面、通量类），包含物理量、区域及分区域标志 |
| `--plotsets` | 图表集配置 |
| `--monitorsets` | 监控集配置 |
| `--iter` | 迭代次数（稳态）或时间步长、时间步数、每步最大迭代次数（瞬态）|

---

## 开源协议

[BSD-3-Clause](LICENSE)
