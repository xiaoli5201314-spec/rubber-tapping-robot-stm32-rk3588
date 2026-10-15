# 四柱升降机构概念脚本

`fusion_script/LiftConcept` 是 Fusion 概念模型生成器。四柱、导轨、丝杆、横移机构和刀头均用于结构布局交流，不是可直接加工采购的工程图。

在 Fusion 的脚本与附加模块中添加 `fusion_script/LiftConcept` 并运行。输出位于本目录 `output/v02`。脚本会创建本地模型并导出 F3D、STEP 和视图；这些产物及个人手改模型不纳入公开仓库。

`RemoveMarkedBlock.py`、`VerifySavedConcept.py` 等是历史编辑/检查辅助脚本，依赖相应实际模型及组件名称。它们不意味着公开生成器能够重建用户后续手动修改后的全部状态。

框架跨度、孔位、导轨、丝杆导程、电机规格、刀具限深/仿形和断电防坠尚需工程设计与验证。
