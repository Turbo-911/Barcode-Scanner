#pragma once
#include "persistence/seasons.h"
#include <string>
#include <vector>

class ISeasonsRepository
{
public:
    virtual ~ISeasonsRepository() = default;
    virtual std::vector<Season> loadAll() = 0;
    virtual bool saveAll(const std::vector<Season>& seasons) = 0;
    virtual std::string filePath() const = 0;
    // Lines lost on the last loadAll (D-03). 0 when the last load was clean.
    virtual int droppedLines() const { return 0; }
};

class SeasonsFileRepository : public ISeasonsRepository
{
public:
    explicit SeasonsFileRepository(std::string fileName = "Data/Seasons.txt");
    std::vector<Season> loadAll() override;
    bool saveAll(const std::vector<Season>& seasons) override;
    std::string filePath() const override { return m_path; }
    int droppedLines() const override { return m_droppedLines; }
private:
    std::string m_path;
    int m_droppedLines = 0;
    bool m_backupWritten = false;
};