//
// Created by lpla on 5/10/20.
//

#ifndef WARC2TEXT_RECORD_HH
#define WARC2TEXT_RECORD_HH

#include <string>
#include <unordered_map>
#include <unordered_set>

class Record {
public:
    Record() : header(), HTTPheader(), payload(), plaintext() {};

    explicit Record(const std::string& content);

    const std::string& getHeaderProperty(const std::string& property) const;
    bool headerExists(const std::string& property) const;

    const std::string& getHTTPheaderProperty(const std::string& property) const;
    bool HTTPheaderExists(const std::string& property) const;

    const std::string& getPayload() const;
    const std::string& getPlainText() const;

    void cleanPayload();
    // std::unordered_map<std::string, std::string> getHeader();

    // std::unordered_map<std::string, std::string> getHTTPHeader();

private:
    std::unordered_map<std::string, std::string> header;
    std::unordered_map<std::string, std::string> HTTPheader;
    std::string payload;
    std::string plaintext;
};

#endif //WARC2TEXT_RECORD_HH
