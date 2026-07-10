#pragma once
// P4:世界情报查询工具族(不上球、纯 AI 工具)。全部经 AsyncJsonFetcher 异步抓取:
// 首调返回 {"pending":true},模型稍后重调命中缓存——描述文案里已教模型这么做。
#include "LayerManager.h"
#include "ai_tools.h"
namespace earthai { class AsyncJsonFetcher; }

void registerWorldQueryTools(earthai::ToolRegistry* tools, LayerManager* layers,
                             earthai::AsyncJsonFetcher* fetcher);
