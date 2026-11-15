#pragma once

#include "Common.h"

bool isValidCode(const string& digits);
vector<int> encodeToWidths(const string& digits);
Image renderBarcode(const string& digits, int module = 3, int height = 120);
