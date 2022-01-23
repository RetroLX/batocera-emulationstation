#pragma once

#include <string>

#include <parallel_hashmap/phmap.h>
using phmap::flat_hash_map;
using phmap::flat_hash_set;

class SystemData;

class LangInfo
{
public:
	static LangInfo parse(std::string rom, SystemData* system);
	static std::string getFlag(const std::string lang, const std::string region);

	std::string region;
    flat_hash_set<std::string> languages;

	std::string getLanguageString();
	bool empty() { return region.empty() && languages.size() == 0; }

private:
	void extractLang(std::string val);
};

