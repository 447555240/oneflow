/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#ifndef ONEFLOW_CORE_FRAMEWORK_SCOPE_UTIL_H_
#define ONEFLOW_CORE_FRAMEWORK_SCOPE_UTIL_H_

#include <vector>
#include "oneflow/core/job/scope.h"

namespace oneflow {

Maybe<Scope> GetCurrentScope();

Maybe<void> InitGlobalScopeStack(const std::shared_ptr<Scope>& scope);

Maybe<void> GlobalScopeStackPush(const std::shared_ptr<Scope>& scope);

Maybe<void> GlobalScopeStackPop();

}  // namespace oneflow

#endif  // ONEFLOW_CORE_FRAMEWORK_SCOPE_UTIL_H_