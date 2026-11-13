#pragma once

#include <string>
#include <utility>
#include <vector>

class UI;

std::vector<std::pair<std::string, std::string>> licenseDocuments();
void openSourceLicenses(UI& ui);
