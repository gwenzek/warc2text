#include <iostream>
#include <vector>
#include <unordered_set>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/algorithm/string/split.hpp>
#include "src/warcpreprocessor.hh"

using namespace warc2text;

struct Options {
    std::vector<std::string> warcs;
    std::string files;
    std::string pdf_warc_filename;
    bool paragraph_identification{};
    bool verbose{};
    bool silent{};
    std::string output;
    std::string tag_filters_filename;
    bool tag_filters_invert{};
    std::string url_filters_filename;
    bool multilang{};
    bool encodeURLs{};
};

void parseArgs(int argc, char *argv[], Options& out) {
    namespace po = boost::program_options;
    po::options_description desc("Arguments");
    desc.add_options()
        ("help,h", po::bool_switch(), "Show this help message")
        ("output,o", po::value(&out.output)->default_value("."), "Output folder")
        ("files,f", po::value(&out.files)->default_value("url,token"), "List of output files separated by commas. Default (mandatory files): 'url,text'. Optional: 'mime,html'")
        ("input,i", po::value(&out.warcs)->multitoken(), "Input WARC file name(s)")
        ("tag-filters", po::value(&out.tag_filters_filename), "Plain text file containing tag filters")
        ("invert-tag-filters", po::bool_switch(&out.tag_filters_invert)->default_value(false), "Invert tag filter application")
        ("url-filters", po::value(&out.url_filters_filename), "Plain text file containing url filters")
        ("pdfpass", po::value(&out.pdf_warc_filename), "Write PDF records to WARC")
        ("paragraph-identification", po::bool_switch(&out.paragraph_identification)->default_value(false), "Add paragraph index in each b64encoded document as tab separated column")
        ("verbose,v", po::bool_switch(&out.verbose)->default_value(false), "Verbosity level")
        ("silent,s", po::bool_switch(&out.silent)->default_value(false))
        ("multilang", po::bool_switch(&out.multilang)->default_value(false), "Detect multiple languages in a single record")
        ("encode-urls", po::bool_switch(&out.encodeURLs)->default_value(false), "Encode URLs obtained from WARC records");

    po::positional_options_description pd;
    pd.add("input", -1);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(pd).run(), vm);
    if (argc == 1 || vm["help"].as<bool>()) {
        std::cerr << "Usage: " << argv[0] << " -o <output_folder> [ -f <output_files> ] [ --pdfpass <output_warc> ] [ --paragraph-identification ] [ --tag-filters <filters_file> ] <warc_file>...\n"
                "\n"
                "Options:\n"
                " -o <output_folder>               Output folder, required\n"
                " -f <output_files>                List of output files separated by commas\n"
                "                                  Default (mandatory): \"url,text\"\n"
                "                                  Optional values: \"mime,html\"\n"
                " --multilang                      Detect multiple languages in documents (up to 3),\n"
                "                                  write as many text records as languages detected\n"
                " --tag-filters <filters_files>    File containing html tag filters\n"
                "                                  Format: \"html_tag <tab> tag_attr <tab> regexp\"\n"
                " --invert-tag-filters             Only output records that got filtered\n"
                " --url-filters <filters_file>     File containing url filters\n"
                "                                  Format: \"regexp\"\n"
                " --pdfpass <output_warc>          Write PDF records to <output_warc>\n"
                " --encode-urls                    Encode URLs obtained from WARC records\n"
                " --paragraph-identification       Add paragraph index for each sentence extracted from the html\n"
                " -s                               Only output errors\n"
                " -v                               Verbose output (print trace)\n\n";
        exit(1);
    }
    po::notify(vm);
}


int main(int argc, char *argv[]) {
    // parse arguments
    Options options;
    parseArgs(argc,argv, options);

    // configure logging
    boost::log::add_console_log(std::cerr, boost::log::keywords::format = "[%TimeStamp%] [\%Severity%] %Message%");
    boost::log::add_common_attributes();
    auto verbosity_level = options.verbose ? boost::log::trivial::trace :
                           options.silent  ? boost::log::trivial::warning :
                                             boost::log::trivial::info;
    boost::log::core::get()->set_filter(boost::log::trivial::severity >= verbosity_level);

    // prepare list of output files
    std::vector<std::string> files_list;
    boost::algorithm::split(files_list, options.files, [](char c) {return c == ',';});
    std::unordered_set<std::string> output_files(files_list.begin(), files_list.end());

    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    WARCPreprocessor warcpproc(options.output, output_files, options.pdf_warc_filename, options.tag_filters_filename,
                               options.tag_filters_invert, options.url_filters_filename, options.multilang,
                               options.encodeURLs, options.paragraph_identification);
    for (const std::string& file : options.warcs){
        warcpproc.process(file);
    }
    warcpproc.printStatistics();
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    unsigned int hours = std::chrono::duration_cast<std::chrono::hours>(end - start).count();
    unsigned int minutes = std::chrono::duration_cast<std::chrono::minutes>(end - start).count() - hours*60;
    unsigned int seconds = std::chrono::duration_cast<std::chrono::seconds>(end - start).count() - hours*60*60 - minutes*60;
    BOOST_LOG_TRIVIAL(info) << "elapsed: " << hours << "h" << minutes << "m" << seconds << "s";

    return 0;
}
