/* SPDX-License-Identifier: GPL-3.0-only */
#include <cstdint>
extern "C" __thread uint32_t test_core_id;
namespace plaits_stmlib {
uint32_t RandomTestCoreId() { return test_core_id; }
}
