// The Naive search algorithm in C++ applied to deferred standoff annotation search in Bitextor pipeline

#include <iostream>
#include <vector>
#include <map>
#include <utility>
#include <fstream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/algorithm/string.hpp>
#include "src/util.hh"

typedef std::vector<std::pair<std::string,std::string>> bitextor_data;

std::tuple<int, int> naivePatternSearch(bitextor_data mainString, int block_index, int pos_index, std::string pattern) {
    int patLen = pattern.size();

    for (int x = block_index; x < int(mainString.size()); x++) {
        for (int i = pos_index; i <= (int(mainString[x].first.size()) - patLen); i++) {
            int j;
            for (j = 0; j < patLen; j++) {      //check for each character of pattern if it is matched
                if (mainString[x].first[i + j] != pattern[j])
                    break;
            }

            if (j == patLen) {     //the pattern is found
                return {x, i};
            }
        }
        pos_index = 0;
    }
    return {-1, -1};
}

// Read Bitextor files for one language and load it into a map<url, html_block vector<text, standoff>>
std::unordered_map<std::string, bitextor_data> loadBitextorData(const std::string& text_path, const std::string& url_path, const std::string& deferred_path) {
    // b64 plain text gzip file
    std::ifstream file_text(text_path, std::ios_base::in | std::ios_base::binary);
    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf_text;
    inbuf_text.push(boost::iostreams::gzip_decompressor());
    inbuf_text.push(file_text);
    //Convert streambuf to istream
    std::istream t_doc_text(&inbuf_text);

    // Url gzip file
    std::ifstream file_url(url_path, std::ios_base::in | std::ios_base::binary);
    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf_url;
    inbuf_url.push(boost::iostreams::gzip_decompressor());
    inbuf_url.push(file_url);
    //Convert streambuf to istream
    std::istream t_doc_url(&inbuf_url);

    // Deferred standoff annotation gzip file
    std::ifstream file_deferred(deferred_path, std::ios_base::in | std::ios_base::binary);
    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf_deferred;
    inbuf_deferred.push(boost::iostreams::gzip_decompressor());
    inbuf_deferred.push(file_deferred);
    //Convert streambuf to istream
    std::istream t_doc_deferred(&inbuf_deferred);

    std::string doc_text;
    std::string doc_url;
    std::string doc_deferred;
    std::unordered_map<std::string, std::vector<std::pair<std::string,std::string>>> blocks;
    while (std::getline(t_doc_url, doc_url))
    {
        std::getline(t_doc_text, doc_text);

        std::string decoded_doc_text;
        util::decodeBase64(doc_text,decoded_doc_text);

        std::getline(t_doc_deferred, doc_deferred);

        std::istringstream ss(decoded_doc_text);
        std::string token;

        std::istringstream sss(doc_deferred);
        std::string standoff;

        while(std::getline(ss, token)) {
            std::getline(sss, standoff, ';');
            std::pair<std::string,std::string> PAIR1;
            PAIR1.first = token;
            PAIR1.second = standoff;
            blocks[doc_url].push_back(PAIR1);
        }
    }
    return blocks;
}


std::string findStandoff(const std::string& block_standoff, int pos_index, int sent_size){
    std::istringstream sss(block_standoff);
    std::string tag_standoff;
    int tag_to;
    std::string final_standoff;
    while(std::getline(sss, tag_standoff, '+')) {
        std::vector<std::string> tag_standoff_split;
        std::vector<std::string> tag_standoff_ranges;
        boost::split(tag_standoff_split, tag_standoff, boost::is_any_of(":"));
        boost::split(tag_standoff_ranges, tag_standoff_split[1], boost::is_any_of("-"));

        tag_to = std::stoi(tag_standoff_ranges[1]);

        if (pos_index < tag_to) {
            std::string reconstructed_standoff = tag_standoff_split[0] + ":" + std::to_string(pos_index) + "-";

            if ((pos_index + sent_size) < tag_to)
                reconstructed_standoff += std::to_string(pos_index + sent_size);
            else{
                reconstructed_standoff += std::to_string(tag_to);
                sent_size -= (tag_to - pos_index);
            }

            if (final_standoff.empty())
                final_standoff = reconstructed_standoff;
            else
                final_standoff += reconstructed_standoff;
        }
        else
            pos_index -= tag_to;
    }
    return final_standoff;
}


int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: Pass plain_text.gz, url.gz and deferred.gz from warc2text for two languages (so 6 files) "
               "to match their content with the lines from a sentence aligner-tab separated file"
               "given as last argument. This will generate a standoff annotation for those sentences or TU\n");return 1;
    }

    std::unordered_map<std::string, bitextor_data> source_blocks = loadBitextorData(argv[1], argv[2], argv[3]);
    std::unordered_map<std::string, bitextor_data> target_blocks = loadBitextorData(argv[4], argv[5], argv[6]);

    // Bitextor segment aligner gzip file
    std::ifstream file(argv[7], std::ios_base::in | std::ios_base::binary);
    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf;
    inbuf.push(boost::iostreams::gzip_decompressor());
    inbuf.push(file);
    //Convert streambuf to istream
    std::istream t_sentences(&inbuf);
    std::string line_sent;
    int pos_index_sl = 0;
    int block_index_sl = 0;
    std::string prev_url_sl;
    int pos_index_tl = 0;
    int block_index_tl = 0;
    std::string prev_url_tl;
    std::string line_parts[15];

    while (std::getline(t_sentences, line_sent))
    {
        std::istringstream ss(line_sent);
        std::string token;
        int i = 0;
        while(std::getline(ss, token, '\t')) {
            line_parts[i] = token;
            i++;
        }

        if (line_parts[0] != prev_url_sl)
        {
            pos_index_sl = 0;
            block_index_sl = 0;
        }
        if (line_parts[1] != prev_url_tl)
        {
            pos_index_tl = 0;
            block_index_tl = 0;
        }

        auto [search_block_index_sl, search_pos_index_sl] = naivePatternSearch(source_blocks[line_parts[0]], block_index_sl, pos_index_sl, line_parts[2]);
        block_index_sl = search_block_index_sl;
        pos_index_sl = search_pos_index_sl;
        auto [search_block_index_tl, search_pos_index_tl] = naivePatternSearch(target_blocks[line_parts[1]], block_index_tl, pos_index_tl, line_parts[3]);
        block_index_tl = search_block_index_tl;
        pos_index_tl = search_pos_index_tl;

        prev_url_sl = line_parts[0];
        prev_url_tl = line_parts[1];

        if (pos_index_sl != -1) {
            std::string found_sl_standoff = findStandoff(source_blocks[line_parts[0]][block_index_sl].second, pos_index_sl,line_parts[2].size());

            std::cout << "SL Sentence '" << line_parts[2] << "' found at block " << block_index_sl << " at position "
                      << pos_index_sl << ". Its final standoff annotation is "
                      << found_sl_standoff << std::endl;
        }
        else {
            std::cout << "SL Sentence could not be found (maybe a glued one from sentence aligner)" << std::endl;
        }

        if (pos_index_tl != -1) {
            std::string found_tl_standoff = findStandoff(target_blocks[line_parts[1]][block_index_tl].second, pos_index_tl,line_parts[3].size());
            std::cout << "TL Sentence '" << line_parts[3] << "' found at block " << block_index_tl << " at position: "
                      << pos_index_tl << ". Its final standoff annotation is "
                      << found_tl_standoff << std::endl;
        }
        else {
            std::cout << "TL Sentence could not be found (maybe a glued one from sentence aligner)" << std::endl;
        }
    }
}
