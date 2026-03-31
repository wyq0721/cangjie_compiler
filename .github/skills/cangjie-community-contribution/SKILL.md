---
name: cangjie-community-contribution
description: 在处理分支创建、提交 commit、编写 PR 描述、贡献自检时使用，遵循仓颉社区 contribution.md 的贡献规范。
---

# 仓颉社区贡献规范

在准备提交 commit、整理 PR 描述或检查贡献流程时，使用本技能约束输出与操作。

## 来源

- 社区贡献规范：`https://gitcode.com/Cangjie/CangjieCommunity/blob/main/contribute/contribution.md`
- 仓库 PR 模板：`.gitcode/PULL_REQUEST_TEMPLATE/PULL_REQUEST_TEMPLATE_chinese.md`

## 使用时机

- 准备提交 commit 前
- 需要拆分或整理提交内容时
- 编写 PR 描述、自检项、关联 issue 时
- 回应代码评审意见时

## 执行要求

1. 基于最新主线创建独立分支开展修改，不直接在主分支上开发。
2. 每个 commit 只解决单一主题，避免把无关修改混在同一次提交中。
3. commit 信息保持简洁明确，直接说明本次变更做了什么。
4. 提交 commit 时必须带 signoff，签名信息固定为 `Signed-off-by: wyq123 <wangyinqiang2@huawei.com>`。
5. 编写 PR 时说明变更目的、主要内容、验证结果，并关联对应 issue。
6. 对评审意见及时响应，按反馈继续补充或修正提交。

## 针对当前仓库的额外检查

- 参考 PR 模板补充自检项：关联 issue
- 如需给出 commit 命令或提交建议，需确保最终提交包含 `Signed-off-by: wyq123 <wangyinqiang2@huawei.com>`

## 输出要求

- 给出 commit 建议时，优先建议单一主题、可读性好的提交标题
- 给出 commit 建议时，同时提醒补齐 signoff 信息 `Signed-off-by: wyq123 <wangyinqiang2@huawei.com>`
- 给出 PR 描述时，覆盖变更内容、变更类型、自检结果、关联 issue
- 若发现修改范围过大，先建议拆分提交，再继续后续操作
