#include "warcpreprocessor.hh"
#include "zipreader.hh"
#include "util/compress.hh"
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace warc2text {
    const std::unordered_set<std::string> WARCPreprocessor::removeExtensions = {".jpg", ".jpeg", ".gif", ".png", ".css", ".js", ".mp3",
                                                                                ".mp4", ".flv", ".wmv", ".gz", ".zip", ".rar" };

    WARCPreprocessor::WARCPreprocessor(const std::string& outputFolder, const std::unordered_set<std::string>& output_files,
                                       const std::string& pdf_warc_filename, const std::string& tagFiltersFile, bool invert,
                                       const std::string& urlFiltersFile, bool multilang, bool encodeURLs,
                                       bool paragraph_identification, bool tsv_output) :
        writer(outputFolder, output_files),
        totalRecords(0),
        textRecords(0),
        langRecords(0),
        totalBytes(0),
        textBytes(0),
        langBytes(0),
        tagFilters(),
        pdf_warc_filename(pdf_warc_filename),
        invert(invert),
        multilang(multilang),
        encodeURLs(encodeURLs),
        paragraph_identification(paragraph_identification),
        tsv_output(tsv_output) {
            if (!tagFiltersFile.empty())
                util::readTagFiltersRegex(tagFiltersFile, tagFilters);

            if (!urlFiltersFile.empty())
                util::readUrlFiltersRegex(urlFiltersFile, urlFilter);
        }

    // true if url is good
    bool WARCPreprocessor::URLfilter(const std::string& url) {
        if (boost::algorithm::ends_with(url, "robots.txt"))
            return false;

        for (const std::string& ext : removeExtensions)
            if (boost::algorithm::ends_with(url, ext))
                return false;

        if (!urlFilter.empty() && boost::regex_search(url, urlFilter)) {
            BOOST_LOG_TRIVIAL(info) << "Url filter matched '" << url << "'";
            return false;
        }
        
        return true;
    }


    void WARCPreprocessor::process(const std::string& filename) {
        BOOST_LOG_TRIVIAL(info) << "Processing " << filename;
        WARCReader reader(filename);

        std::string content;
        bool done = false;
        int n_langs = 0;

        bool pdfpass = !pdf_warc_filename.empty();
        WARCWriter pdf_warc_writer;

        while (!done) {
            done = !reader.getRecord(content);
            ++totalRecords;

            if (done or content.empty())
                continue;

            Record record(content);
            if (record.getPayload().empty())
                continue;

            if (record.getRecordType() != "response" && record.getRecordType() != "resource")
                continue;

            if (record.getWARCcontentType().find("application/http") == std::string::npos)
                continue;

            // if HTTP content type is 'text/html' or something similar, don't rely on URL extension to detect unprocessed PDFs
            // PDFs that have gone through bitextor-warc2htmlwarc.py will have URL ending in .pdf but text HTTP content type
            if (not record.isTextFormat() and (boost::algorithm::ends_with(record.getURL(), ".pdf") or record.getHTTPcontentType() == "application/pdf")) {
                // found a PDF file, write record to disk and continue
                if (pdfpass) {
                    // Work-around for https://github.com/bitextor/warc2text/issues/16 for ParaCrawl
                    // we do not really have a use case for massive PDFs at this moment. Skip em.
                    if (content.size() >= static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
                        BOOST_LOG_TRIVIAL(info) << "PDF too large to compress with util::GZCompress";
                        continue;
                    }

                    if (!pdf_warc_writer.is_open())
                        pdf_warc_writer.open(pdf_warc_filename);

                    pdf_warc_writer.writeRecord(content);
                }
                continue;
            }

            if (record.getPayload().size() > 5242880) // 5MB
                continue;

            if (!URLfilter(record.getURL()))
                continue;

            if (encodeURLs)
                record.encodeURL();

            BOOST_LOG_TRIVIAL(trace) << "Processing HTML document " << record.getURL() << "\n";

            totalBytes += record.getPayload().size();

            int clean_retval;
            try{
                clean_retval = record.cleanPayload(tagFilters);
            }
            catch (std::out_of_range& e) { continue; }
            catch (std::invalid_argument& e) { continue; }
            catch (util::ZipReadError& e) {
                BOOST_LOG_TRIVIAL(info) << "Record " << record.getURL() << " discarded due to invalid zip file: " << e.what();
                continue;
            }

            if ((clean_retval == util::FILTERED_DOCUMENT_ERROR) != invert) {
                BOOST_LOG_TRIVIAL(info) << "Record " << record.getURL() << " discarded due to tag filters";
                continue;
            } else if (clean_retval == util::HTML_PARSING_ERROR) {
                BOOST_LOG_TRIVIAL(trace) << "Record " << record.getURL() << ": parsing error";
                continue;
            } else if (clean_retval == util::UNKNOWN_ENCODING_ERROR) {
                BOOST_LOG_TRIVIAL(trace) << "Record " << record.getURL() << ": unknown encoding";
                continue;
            } else if (clean_retval == util::UTF8_CONVERSION_ERROR) {
                BOOST_LOG_TRIVIAL(trace) << "Record " << record.getURL() << ": utf8 conversion error";
                continue;
            } else if (clean_retval == util::NOT_VALID_RECORD) {
                BOOST_LOG_TRIVIAL(trace) << "Record " << record.getURL() << ": WARC or HTTP header content type not valid";
                continue;
            }

            if (record.getPlainText().empty()) {
                BOOST_LOG_TRIVIAL(trace) << "Record " << record.getURL() << ": empty";
                continue;
            }

            ++textRecords;
            textBytes += record.getPlainText().size();

            if (!tsv_output) {
                n_langs = record.detectLanguage(multilang);
                if (n_langs == 1) {
                    langBytes += record.getPlainText().size();
                } else if (n_langs > 1) {
                    BOOST_LOG_TRIVIAL(trace) << "Record " << record.getURL() << ": multiple (" << n_langs << ") languages detected";
                    for (auto it : record.getTextByLangs())
                        langBytes += it.second.size();
                } else {
                    BOOST_LOG_TRIVIAL(trace) << "Record " << record.getURL() << ": language not detected";
                    continue;
                }

                langRecords += n_langs;
            } else {
                writer.write_tsv(record);
            }

        }
        pdf_warc_writer.close();
    }

    void WARCPreprocessor::printStatistics() const{
        BOOST_LOG_TRIVIAL(info) << "total records: " << totalRecords;
        BOOST_LOG_TRIVIAL(info) << "text records: " << textRecords;
        BOOST_LOG_TRIVIAL(info) << "lang records: " << langRecords;

        BOOST_LOG_TRIVIAL(info) << "total bytes: " << totalBytes;
        BOOST_LOG_TRIVIAL(info) << "text bytes: " << textBytes;
        BOOST_LOG_TRIVIAL(info) << "lang bytes: " << langBytes;
    }

    WARCWriter::WARCWriter() {
        warc = nullptr;
    }

    void WARCWriter::open(const std::string& warc_filename) {
        filename = warc_filename;
        if (not boost::algorithm::ends_with(filename, ".warc.gz"))
            filename += ".warc.gz";
        std::string folder = filename.substr(0, filename.find_last_of('/'));
        util::createDirectories(folder);
        warc = std::fopen(filename.c_str(), "wb");
    }

    bool WARCWriter::is_open() {
        return warc != nullptr;
    }

    void WARCWriter::close() {
        if (warc) std::fclose(warc);
    }

    void WARCWriter::writeRecord(const std::string& content) {
        if (!warc) return;
        std::string compressed;
        util::GZCompress(content, compressed);
        std::fwrite((void*) compressed.c_str(), 1, compressed.size(), warc);
    }

}
