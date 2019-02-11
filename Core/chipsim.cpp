/*
Copyright (c) 2010 Brian Silverman, Barry Silverman

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "stdafx.h"
#include <iostream>
#include <cstring>
#include "datastructures.h"
#include "chipsim.h"
#include "wires.h"
#include "datadefs.h"
#include <assert.h>

vector<uint16_t> processedNodes;
shared_ptr<vector<uint16_t>> recalclists[2];
shared_ptr<vector<uint16_t>> recalclist;
vector<uint16_t> group;

bool groupEmpty = true;
bool hasGnd = false;
bool hasPwr = false;

int recalcNodeListCount = 0;
int j = 0;
int line = 0;
//std::ofstream tracefile;
void recalcNodeList(shared_ptr<vector<uint16_t>> list) {
	//if (!tracefile.is_open()) {
	//	tracefile.open("C:\\Users\\bgour\\Desktop\\trace.txt");
	//}
	recalcNodeListCount++;
	if(processedNodes.empty()) {
		processedNodes.insert(processedNodes.end(), nodes.size(), 0);
		recalclists[0].reset(new vector<uint16_t>(100));
		recalclists[1].reset(new vector<uint16_t>(100));
	} else {
		recalclists[0]->clear();
	}
	recalclist = recalclists[0];

	for(j = 0; j<100; j++) {		// loop limiter
		if(j == 99) {
			throw std::runtime_error("Maximum loop exceeded");
		}

		line = 0;
		for(int nodeNumber : *list) {
			line++;
			recalcNode(nodeNumber);

			//tracefile << "nn=" << nodeNumber << " c=" << recalcNodeListCount << " j=" << j << " l=" << line << ":";

			//for (int i = 0; i < group.size(); i++) {
			//	if (i > 0) {
			//		tracefile << ",";
			//	}
			//	tracefile << group[i];
			//}
			//tracefile << std::endl;
		}

		if (groupEmpty) {
			return;
		}
			
		for(int nodeNumber : *recalclist) {
			processedNodes[nodeNumber] = 0;
		}

		list = recalclist;
		recalclist = (recalclist == recalclists[1]) ? recalclists[0] : recalclists[1];
		recalclist->clear();

		groupEmpty = true;
	}
}

void recalcNode(uint16_t nodeNumber) {
	if(nodeNumber == ngnd) return;
	if(nodeNumber == npwr) return;
	getNodeGroup(nodeNumber);
	bool newState = getNodeValue();

	for(uint16_t nn : group) {
		node& n = nodes[nn];
		if(n.state != newState) {
			n.state = newState;
			for(uint16_t i : n.gates) {
				if (n.state) {
					if (i == 17236) {
						std::cout << "turning trans 17236 on (c: " << recalcNodeListCount << " j:" << j << " nn:" << nn << " l:" << line << ")" << std::endl;
					}
					turnTransistorOn(i);
				}
				else {
					if (i == 17236) {
						std::cout << "turning trans 17236 off (c: " << recalcNodeListCount << " j:" << j << " nn:" << nn << " l:" << line << ")" << std::endl;
					}
					turnTransistorOff(i);
				}
			}
		}
	}
}

void turnTransistorOn(uint16_t i) {
	transistor &t = transistors[i];
	if(t.on) return;
	t.on = true;
	addRecalcNode(t.c1);
}

void turnTransistorOff(uint16_t i) {
	transistor &t = transistors[i];
	if(!t.on) return;
	t.on = false;
	addRecalcNode(t.c1);
	addRecalcNode(t.c2);
}

void addRecalcNode(uint16_t nn) {
	if(nn == ngnd) return;
	if(nn == npwr) return;

	if (recalcNodeListCount == 20 && j == 0 && nn == 4546) {
		//std::cout << "ahhh";
	}

	if(!processedNodes[nn]) {
		recalclist->push_back(nn);
		processedNodes[nn] = 1;
	}

	groupEmpty = false;
}

void getNodeGroup(uint16_t nn) {
	hasGnd = false;
	hasPwr = false;
	group.clear();
	addNodeToGroup(nn, 0);
}

void addNodeToGroup(uint16_t nn, uint64_t recurseCount) {
	if(nn == ngnd) {
		hasGnd = true;
		return;
	}
	if(nn == npwr) {
		hasPwr = true;
		return;
	}


	if(find(group.begin(), group.end(), nn) != group.end()) return;
	group.push_back(nn);

	for(uint16_t i = 0, len = nodeCount[nn]; i < len; i++) {
		auto t_index = nodeC1c2s[nn][i];
		transistor &t = transistors[nodeC1c2s[nn][i]];

		if (recalcNodeListCount == 20 && j == 0 && nn == 23146 && line == 10536) {
			//std::cout << "Nessim state is wrong here, transistor is on when should be off" << std::endl;
		}

		if(t.on) {
			addNodeToGroup(t.c1 == nn ? t.c2 : t.c1, recurseCount + 1);
		}
	}
}

bool getNodeValue() {
	if(hasGnd && hasPwr) {
		for(uint16_t i : group) {
			if(i == 359 || i == 566 || i == 691 || i == 871 || i == 870 || i == 864 || i == 856 || i == 818) {
				hasGnd = hasPwr = false;
				break;
			}
		}
	}

	if(hasGnd) {
		return false;
	} else if(hasPwr) {
		return true;
	}

	int hi_area = 0;
	int lo_area = 0;
	for(uint16_t nn : group) {
		node &n = nodes[nn];
		if(n.pullup) return true;
		if(n.pulldown) return false;
		if(n.state) hi_area += n.area;
		else lo_area += n.area;
	}
	return (hi_area > lo_area);
}

bool isNodeHigh(uint16_t nn) {
	return(nodes[nn].state);
}

bool isTransistorOn(char* transistorName)
{
	return transistors[transistorIndexByName[transistorName]].on;
}

shared_ptr<vector<uint16_t>> allNodes() {
	shared_ptr<vector<uint16_t>> result(new vector<uint16_t>());
	for(node& node : nodes) {
		if(node.num != npwr && node.num != ngnd && node.num != EMPTYNODE) {
			result->push_back(node.num);
		}
	}
	return result;
}

string getStateString() {
	char codes[2] = { 'l', 'h' };
	string res;
	for(node& n : nodes) {
		if(n.num < 0) res += 'x';
		else if(n.num == ngnd) res += 'g';
		else if(n.num == npwr) res += 'v';
		else res += codes[n.state ? 1 : 0];
	}
	return res;
}

void setState(string str) {
	unordered_map<char, bool> codes = { {'g', false}, {'h', true}, {'v',true}, {'l', false } };
	for(size_t i = 0; i < str.size(); i++) {
		char c = str[i];
		if(c == 'x') continue;
		bool state = codes[c];
		if(nodes[i].num < 0) continue;

		nodes[i].state = state;
		for(uint16_t i : nodes[i].gates) {
			transistors[i].on = state;
		}
	}
}

void setFloat(string name) {
	uint16_t nn = nodenames[name];
	nodes[nn].pullup = false;
	nodes[nn].pulldown = false;
	recalcNodeList(shared_ptr<vector<uint16_t>>(new vector<uint16_t> { nn }));
}

void setHigh(string name) {
	uint16_t nn = nodenames[name];
	nodes[nn].pullup = true;
	nodes[nn].pulldown = false;
	recalcNodeList(shared_ptr<vector<uint16_t>>(new vector<uint16_t>{ nn }));
}

void setLow(string name) {
	uint16_t nn = nodenames[name];
	nodes[nn].pullup = false;
	nodes[nn].pulldown = true;
	recalcNodeList(shared_ptr<vector<uint16_t>>(new vector<uint16_t>{ nn }));
}