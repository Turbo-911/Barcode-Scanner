#pragma once
#include "common/common.h"
bool saveBMP(const Image& img, const std::string& path);
bool loadBMP(const std::string& path, Image& out);
