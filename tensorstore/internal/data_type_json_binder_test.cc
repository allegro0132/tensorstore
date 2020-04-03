// Copyright 2020 The TensorStore Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tensorstore/internal/data_type_json_binder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/status/status.h"
#include <nlohmann/json.hpp>
#include "tensorstore/data_type.h"
#include "tensorstore/internal/json_bindable.h"
#include "tensorstore/internal/json_gtest.h"
#include "tensorstore/util/status_testutil.h"

namespace {
using tensorstore::DataType;
using tensorstore::DataTypeOf;
using tensorstore::MatchesStatus;

namespace jb = tensorstore::internal::json_binding;

struct X {};

TEST(DataTypeJsonBinderTest, ToJson) {
  EXPECT_THAT(jb::ToJson(DataType(DataTypeOf<tensorstore::int32_t>())),
              ::testing::Optional(::nlohmann::json("int32")));
  EXPECT_THAT(jb::ToJson(DataType(DataTypeOf<bool>())),
              ::testing::Optional(::nlohmann::json("bool")));
  EXPECT_THAT(jb::ToJson(DataType(DataTypeOf<X>())),
              MatchesStatus(absl::StatusCode::kInvalidArgument,
                            "Data type has no canonical identifier"));
  EXPECT_THAT(jb::ToJson(DataType{}),
              ::testing::Optional(tensorstore::MatchesJson(
                  ::nlohmann::json(::nlohmann::json::value_t::discarded))));
}

TEST(DataTypeJsonBinderTest, FromJson) {
  EXPECT_THAT(jb::FromJson<DataType>(::nlohmann::json("int32")),
              ::testing::Optional(DataTypeOf<std::int32_t>()));
  EXPECT_THAT(jb::FromJson<DataType>(::nlohmann::json("bool")),
              ::testing::Optional(DataTypeOf<bool>()));
  EXPECT_THAT(jb::FromJson<DataType>(::nlohmann::json("invalid")),
              MatchesStatus(absl::StatusCode::kInvalidArgument,
                            "Unsupported data type: \"invalid\""));
  EXPECT_THAT(jb::FromJson<DataType>(
                  ::nlohmann::json(::nlohmann::json::value_t::discarded)),
              ::testing::Optional(DataType{}));
  EXPECT_THAT(jb::FromJson<DataType>(
                  ::nlohmann::json(::nlohmann::json::value_t::discarded),
                  tensorstore::internal::json_binding::DataTypeJsonBinder),
              MatchesStatus(absl::StatusCode::kInvalidArgument));
}

}  // namespace
