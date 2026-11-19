#pragma once
#include "common/common.h"
bool saveBMP(const Image& img, const string& path);
bool loadBMP(const string& path, Image& out);
