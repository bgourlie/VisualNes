#pragma once
#include "datastructures.h"

extern vector<vector<int64_t>> segdefs;
extern vector<transdef> transdefs;
extern unordered_map<string, uint16_t> nodenames;
extern vector<vector<vector<int>>> palette_nodes;
extern vector<vector<vector<int>>> sprite_nodes;
extern unordered_map<uint16_t, uint16_t> idConvertTable;
vector<string> split(const string &s, char delim);
void loadDataDefinitions();
