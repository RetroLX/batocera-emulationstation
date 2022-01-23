#pragma once
#ifndef ES_APP_FILE_FILTER_INDEX_H
#define ES_APP_FILE_FILTER_INDEX_H

#include <vector>
#include <string>

#include <parallel_hashmap/phmap.h>
using phmap::flat_hash_map;
using phmap::flat_hash_set;

class FileData;
class SystemData;

enum FilterIndexType
{
	NONE = 0,
	GENRE_FILTER = 1,
	FAMILY_FILTER = 2,
	PLAYER_FILTER = 3,
	PUBDEV_FILTER = 4,
	RATINGS_FILTER = 5,
	YEAR_FILTER = 6,
	KIDGAME_FILTER = 7,
	HIDDEN_FILTER = 8,
	PLAYED_FILTER = 9,
	LANG_FILTER = 10,
	REGION_FILTER = 11,
	FAVORITES_FILTER = 12,
	CHEEVOS_FILTER = 13,
	VERTICAL_FILTER = 14
};

struct FilterDataDecl
{
	FilterIndexType type; // type of filter
    flat_hash_map<std::string, int>* allIndexKeys; // all possible filters for this type
	bool* filteredByRef; // is it filtered by this type
    flat_hash_set<std::string>* currentFilteredKeys; // current keys being filtered for
	std::string primaryKey; // primary key in metadata
	bool hasSecondaryKey; // has secondary key for comparison
	std::string secondaryKey; // what's the secondary key
	std::string menuLabel; // text to show in menu
};

class FileFilterIndex
{
	friend class CollectionFilter;

private:
	FileFilterIndex(const FileFilterIndex&) { };
	FileFilterIndex& operator=(FileFilterIndex const&) { return *this; }

public:
	FileFilterIndex();
	~FileFilterIndex();

	void addToIndex(FileData* game);
	void removeFromIndex(FileData* game);
	void setFilter(FilterIndexType type, std::vector<std::string>* values);
    flat_hash_set<std::string>* getFilter(FilterIndexType type);

	void clearAllFilters();
	
	virtual int showFile(FileData* game);
	virtual bool isFiltered() { return (!mTextFilter.empty() || filterByGenre || filterByPlayers || filterByPubDev || filterByFamily
		|| filterByRatings || filterByFavorites || filterByKidGame || filterByPlayed || filterByLang || filterByRegion || filterByYear || filterByCheevos || filterByVertical); };

	bool isKeyBeingFilteredBy(std::string key, FilterIndexType type);
	std::vector<FilterDataDecl> getFilterDataDecls();

	void importIndex(FileFilterIndex* indexToImport);
	void copyFrom(FileFilterIndex* indexToImport);

	void resetIndex();
	void resetFilters();
	void setUIModeFilters();

	void setTextFilter(const std::string text, bool useRelevancy = false);
	inline const std::string getTextFilter() { return mTextFilter; }
	inline bool hasRelevency() { return !mTextFilter.empty() && mUseRelevency; }

protected:
	//std::vector<FilterDataDecl> filterDataDecl;
    flat_hash_map<int, FilterDataDecl> mFilterDecl;

	std::string getIndexableKey(FileData* game, FilterIndexType type, bool getSecondary);

	void manageGenreEntryInIndex(FileData* game, bool remove = false);
	void manageFamilyEntryInIndex(FileData* game, bool remove = false);
	void managePlayerEntryInIndex(FileData* game, bool remove = false);
	void managePubDevEntryInIndex(FileData* game, bool remove = false);	
	void manageYearEntryInIndex(FileData* game, bool remove = false);
	void manageIndexEntry(flat_hash_map<std::string, int>* index, const std::string& key, bool remove, bool forceUnknown = false);

	void manageLangEntryInIndex(FileData* game, bool remove = false);
	void manageRegionEntryInIndex(FileData* game, bool remove = false);

	void clearIndex(flat_hash_map<std::string, int> indexMap);

	bool filterByGenre;
	bool filterByFamily;
	bool filterByPlayers;
	bool filterByPubDev;
	bool filterByRatings;
	bool filterByFavorites;
	bool filterByYear;
	bool filterByKidGame;
	bool filterByPlayed;
	bool filterByLang;
	bool filterByRegion;
	bool filterByCheevos;
	bool filterByVertical;

    flat_hash_map<std::string, int> genreIndexAllKeys;
    flat_hash_map<std::string, int> familyIndexAllKeys;
    flat_hash_map<std::string, int> playersIndexAllKeys;
    flat_hash_map<std::string, int> pubDevIndexAllKeys;
    flat_hash_map<std::string, int> ratingsIndexAllKeys;
    flat_hash_map<std::string, int> favoritesIndexAllKeys;
    flat_hash_map<std::string, int> yearIndexAllKeys;
    flat_hash_map<std::string, int> kidGameIndexAllKeys;
    flat_hash_map<std::string, int> playedIndexAllKeys;
    flat_hash_map<std::string, int> langIndexAllKeys;
    flat_hash_map<std::string, int> regionIndexAllKeys;
    flat_hash_map<std::string, int> cheevosIndexAllKeys;

    flat_hash_map<std::string, int> verticalIndexAllKeys;

    flat_hash_set<std::string> genreIndexFilteredKeys;
    flat_hash_set<std::string> familyIndexFilteredKeys;
    flat_hash_set<std::string> playersIndexFilteredKeys;
    flat_hash_set<std::string> pubDevIndexFilteredKeys;
    flat_hash_set<std::string> ratingsIndexFilteredKeys;
    flat_hash_set<std::string> favoritesIndexFilteredKeys;
    flat_hash_set<std::string> yearIndexFilteredKeys;
    flat_hash_set<std::string> kidGameIndexFilteredKeys;
    flat_hash_set<std::string> playedIndexFilteredKeys;
    flat_hash_set<std::string> langIndexFilteredKeys;
    flat_hash_set<std::string> regionIndexFilteredKeys;
    flat_hash_set<std::string> cheevosIndexFilteredKeys;
    flat_hash_set<std::string> verticalIndexFilteredKeys;

	std::string mTextFilter;
	bool		mUseRelevency;
};

class CollectionFilter : public FileFilterIndex
{
public:	
	bool create(const std::string name);
	bool createFromSystem(const std::string name, SystemData* system);

	bool load(const std::string file);
	bool save();

	int showFile(FileData* game) override;
	bool isFiltered() override;

	bool isSystemSelected(const std::string name);
	void setSystemSelected(const std::string name, bool value);
	void resetSystemFilter();

protected:
	std::string mName;
	std::string mPath;
    flat_hash_set<std::string> mSystemFilter;
};

#endif // ES_APP_FILE_FILTER_INDEX_H
