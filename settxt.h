#pragma once

#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <map>

using namespace std;


class settxt
{
public:
	settxt(const string& txtname) { getTxtmap(txtname); };
	void getTxtmap(const string& txtname);
	string getValue(const string& key, string defValue = "");

private:
	map<string, string> txtmap;
};

