#include "variant_annotation.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace biocore::variant_annotation {
namespace {
constexpr std::size_t max_line = 64U * 1024U * 1024U;
constexpr std::uint64_t max_annotation_rows = 5'000'000U;
constexpr std::uint64_t max_vcf_records = 50'000'000U;

std::vector<std::string> split(const std::string_view text, const char delimiter) {
    std::vector<std::string> out; std::size_t start=0;
    while (true) { const auto pos=text.find(delimiter,start); out.emplace_back(text.substr(start,pos==std::string_view::npos?text.size()-start:pos-start)); if(pos==std::string_view::npos) break; start=pos+1; }
    return out;
}
std::string json_escape(std::string_view value) {
    std::string out; out.reserve(value.size()+8U);
    constexpr char hex[]="0123456789abcdef";
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        switch(c){case '"':out+="\\\"";break;case '\\':out+="\\\\";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default: if(c<0x20U){out+="\\u00";out+=hex[(c>>4U)&0xfU];out+=hex[c&0xfU];}else out+=static_cast<char>(c);}
    }
    return out;
}
std::string html_escape(std::string_view value) {
    std::string out; out.reserve(value.size()+8U);
    for(char c:value){switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:out+=c;}}
    return out;
}
std::string vcf_escape(std::string_view value) {
    static constexpr char hex[]="0123456789ABCDEF";
    std::string out;
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        const bool safe=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c==':'||c=='/'||c=='+'||c=='-';
        if(safe) out+=static_cast<char>(c); else { out+='%'; out+=hex[(c>>4U)&0xfU]; out+=hex[c&0xfU]; }
    }
    return out.empty()?".":out;
}
std::string tsv_safe(std::string_view value) {
    std::string out; out.reserve(value.size());
    for(char c:value) out += (c=='\t'||c=='\n'||c=='\r') ? ' ' : c;
    return out;
}
std::uint64_t parse_pos(std::string_view value) {
    std::uint64_t result{}; const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0U) {
        throw std::invalid_argument("VCF POS is invalid");
    }
    return result;
}
std::string annotation_key(std::string_view chrom, std::string_view pos, std::string_view ref, std::string_view alt) {
    return std::string{chrom}+":"+std::string{pos}+":"+std::string{ref}+":"+std::string{alt};
}
std::vector<std::pair<std::string,std::uint64_t>> top_counts(const std::unordered_map<std::string,std::uint64_t>& values) {
    std::vector<std::pair<std::string,std::uint64_t>> out(values.begin(),values.end());
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.second==b.second?a.first<b.first:a.second>b.second;});
    if (out.size() > 10U) out.resize(10U);
    return out;
}
std::string counts_json(const std::unordered_map<std::string,std::uint64_t>& values) {
    const auto top=top_counts(values); std::string out="[";
    for(std::size_t i=0;i<top.size();++i){if(i)out+=',';out+="{\"name\":\""+json_escape(top[i].first)+"\",\"count\":"+std::to_string(top[i].second)+"}";}
    return out+"]";
}
}

std::unordered_map<std::string, AnnotationRecord> load_annotation_table(std::istream& input, std::uint64_t& rows) {
    std::unordered_map<std::string, AnnotationRecord> result; rows=0U; std::string line;
    if(!std::getline(input,line)) throw std::invalid_argument("Annotation TSV is empty");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line != "key\tgene\tconsequence\tclinical_significance\tsource\tsource_id") throw std::invalid_argument("Annotation TSV header is invalid");
    while(std::getline(input,line)){
        if(line.size()>max_line) throw std::invalid_argument("Annotation TSV line is too long");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto fields=split(line,'\t'); if(fields.size()!=6U||fields[0].empty()) throw std::invalid_argument("Annotation TSV row is invalid");
        if(++rows>max_annotation_rows) throw std::invalid_argument("Annotation TSV row safety limit exceeded");
        AnnotationRecord record{fields[1],fields[2],fields[3],fields[4],fields[5]};
        if(!result.emplace(fields[0],std::move(record)).second) throw std::invalid_argument("Annotation TSV key is duplicated");
    }
    if(input.bad()) throw std::runtime_error("Unable to read annotation TSV");
    return result;
}

AnnotationResult annotate_vcf(std::istream& vcf, const std::unordered_map<std::string, AnnotationRecord>& annotations, const std::uint64_t annotation_rows) {
    AnnotationResult result; result.statistics.annotation_rows=annotation_rows;
    std::ostringstream annotated, table, report_rows; std::string line; bool header_seen=false; bool format_seen=false; std::uint64_t report_count=0U;
    table << "key\tchrom\tpos\tref\talt\tfilter\tannotation_hit\tgene\tconsequence\tclinical_significance\tsource\tsource_id\n";
    while(std::getline(vcf,line)){
        if(line.size()>max_line) throw std::invalid_argument("VCF line is too long");
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.rfind("##fileformat=VCFv4.",0)==0){format_seen=true; annotated<<line<<'\n'; continue;}
        if(line.rfind("##",0)==0){ if(header_seen) throw std::invalid_argument("VCF metadata appears after header"); annotated<<line<<'\n'; continue; }
        if(line.rfind("#CHROM\t",0)==0){
            if (!format_seen || header_seen) throw std::invalid_argument("VCF header is invalid");
            header_seen = true;
            annotated << "##INFO=<ID=BC_GENE,Number=A,Type=String,Description=\"OpenGenesis-BioCore local annotation gene\">\n"
                      << "##INFO=<ID=BC_CSQ,Number=A,Type=String,Description=\"OpenGenesis-BioCore local annotation consequence\">\n"
                      << "##INFO=<ID=BC_CLNSIG,Number=A,Type=String,Description=\"OpenGenesis-BioCore local annotation clinical significance\">\n"
                      << "##INFO=<ID=BC_SOURCE,Number=A,Type=String,Description=\"OpenGenesis-BioCore local annotation source\">\n"
                      << "##INFO=<ID=BC_SOURCE_ID,Number=A,Type=String,Description=\"OpenGenesis-BioCore local annotation source identifier\">\n"
                      << "##BioCoreVariantAnnotation=<Version=0.1.0,Key=CHROM:POS:REF:ALT,Normalization=none,Mode=local-tsv>\n"
                      << line << '\n'; continue;
        }
        if (line.empty()) continue;
        if (!header_seen) throw std::invalid_argument("VCF record appears before header");
        if(++result.statistics.vcf_records>max_vcf_records) throw std::invalid_argument("VCF record safety limit exceeded");
        auto fields=split(line,'\t'); if(fields.size()<8U) throw std::invalid_argument("VCF record has fewer than eight fields");
        static_cast<void>(parse_pos(fields[1]));
        const auto alts=split(fields[4],','); if(alts.empty()||fields[3].empty()||fields[4].empty()||fields[4]==".") throw std::invalid_argument("VCF REF/ALT is invalid");
        const bool pass=fields[6]=="PASS"; if(pass)++result.statistics.pass_records;else ++result.statistics.filtered_records;
        std::array<std::vector<std::string>,5> info_values;
        for(const auto& alt:alts){
            ++result.statistics.alt_alleles; const auto key=annotation_key(fields[0],fields[1],fields[3],alt); const auto it=annotations.find(key); const bool hit=it!=annotations.end();
            if(hit){++result.statistics.annotation_hits; const auto&r=it->second; info_values[0].push_back(vcf_escape(r.gene));info_values[1].push_back(vcf_escape(r.consequence));info_values[2].push_back(vcf_escape(r.clinical_significance));info_values[3].push_back(vcf_escape(r.source));info_values[4].push_back(vcf_escape(r.source_id)); if(!r.gene.empty()&&r.gene!=".")++result.statistics.genes[r.gene];if(!r.consequence.empty()&&r.consequence!=".")++result.statistics.consequences[r.consequence];if(!r.clinical_significance.empty()&&r.clinical_significance!=".")++result.statistics.clinical_significance[r.clinical_significance];
                table<<key<<'\t'<<fields[0]<<'\t'<<fields[1]<<'\t'<<fields[3]<<'\t'<<alt<<'\t'<<fields[6]<<"\t1\t"<<tsv_safe(r.gene)<<'\t'<<tsv_safe(r.consequence)<<'\t'<<tsv_safe(r.clinical_significance)<<'\t'<<tsv_safe(r.source)<<'\t'<<tsv_safe(r.source_id)<<'\n';
                if(report_count<500U){report_rows<<"<tr><td><code>"<<html_escape(key)<<"</code></td><td>"<<html_escape(fields[6])<<"</td><td>"<<html_escape(r.gene)<<"</td><td>"<<html_escape(r.consequence)<<"</td><td>"<<html_escape(r.clinical_significance)<<"</td><td>"<<html_escape(r.source)<<"</td></tr>";++report_count;}
            }else{++result.statistics.annotation_misses; for(auto&v:info_values)v.push_back("."); table<<key<<'\t'<<fields[0]<<'\t'<<fields[1]<<'\t'<<fields[3]<<'\t'<<alt<<'\t'<<fields[6]<<"\t0\t.\t.\t.\t.\t.\n";}
        }
        auto join=[](const std::vector<std::string>& values){std::string out;for(std::size_t i=0;i<values.size();++i){if(i)out+=',';out+=values[i];}return out;};
        std::string extra="BC_GENE="+join(info_values[0])+";BC_CSQ="+join(info_values[1])+";BC_CLNSIG="+join(info_values[2])+";BC_SOURCE="+join(info_values[3])+";BC_SOURCE_ID="+join(info_values[4]);
        fields[7]=(fields[7]=="."||fields[7].empty())?extra:fields[7]+";"+extra;
        for(std::size_t i=0;i<fields.size();++i){if(i)annotated<<'\t';annotated<<fields[i];}annotated<<'\n';
    }
    if (vcf.bad()) throw std::runtime_error("Unable to read VCF");
    if (!header_seen) throw std::invalid_argument("VCF column header is missing");
    const double hit_rate=result.statistics.alt_alleles==0U?0.0:100.0*static_cast<double>(result.statistics.annotation_hits)/static_cast<double>(result.statistics.alt_alleles);
    std::ostringstream report;
    report<<"<!doctype html><html><head><meta charset=\"utf-8\"><title>OpenGenesis-BioCore Variant Annotation Report</title><style>body{font-family:system-ui,sans-serif;margin:2rem;color:#202124}table{border-collapse:collapse;width:100%}th,td{border:1px solid #ddd;padding:.45rem;text-align:left}th{background:#f4f4f4}code{font-size:.9em}.metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:1rem;margin:1rem 0}.metric{border:1px solid #ddd;padding:1rem;border-radius:8px}</style></head><body><main><h1>OpenGenesis-BioCore Variant Annotation Report</h1><p>Local-only annotation · raw key CHROM:POS:REF:ALT · normalization: none.</p><div class=\"metrics\"><div class=\"metric\"><strong>ALT alleles</strong><div>"<<result.statistics.alt_alleles<<"</div></div><div class=\"metric\"><strong>Annotated</strong><div>"<<result.statistics.annotation_hits<<"</div></div><div class=\"metric\"><strong>Unannotated</strong><div>"<<result.statistics.annotation_misses<<"</div></div><div class=\"metric\"><strong>Hit rate</strong><div>"<<hit_rate<<"%</div></div></div><h2>Annotated variants</h2><p>Showing up to 500 annotation hits. Full results are available in TSV/VCF artifacts.</p><table><thead><tr><th>Key</th><th>FILTER</th><th>Gene</th><th>Consequence</th><th>Clinical significance</th><th>Source</th></tr></thead><tbody>"<<report_rows.str()<<"</tbody></table></main></body></html>";
    result.annotated_vcf=annotated.str(); result.table_tsv=table.str(); result.report_html=report.str(); return result;
}

std::string render_annotation_json(const AnnotationStatistics& s) {
    const double hit=s.alt_alleles==0U?0.0:100.0*static_cast<double>(s.annotation_hits)/static_cast<double>(s.alt_alleles);
    std::ostringstream out; out<<"{\"schemaVersion\":1,\"algorithm\":\"local-raw-key-annotation-v1\",\"normalization\":\"none\",\"annotationRows\":"<<s.annotation_rows<<",\"vcfRecords\":"<<s.vcf_records<<",\"altAlleles\":"<<s.alt_alleles<<",\"annotationHits\":"<<s.annotation_hits<<",\"annotationMisses\":"<<s.annotation_misses<<",\"annotationHitRatePercent\":"<<hit<<",\"passRecords\":"<<s.pass_records<<",\"filteredRecords\":"<<s.filtered_records<<",\"topGenes\":"<<counts_json(s.genes)<<",\"topConsequences\":"<<counts_json(s.consequences)<<",\"topClinicalSignificance\":"<<counts_json(s.clinical_significance)<<"}"; return out.str();
}
}
