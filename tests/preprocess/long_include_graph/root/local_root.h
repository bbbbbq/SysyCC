#pragma once

#include "left_branch.h"
#include "right_branch.h"
#include "nested_local.h"
#include "local_chain.h"

int combined_total = shared_once_value + from_left + from_right + nested_local_value + layered_total;
