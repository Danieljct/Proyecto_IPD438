// Offline evaluator: reads a packet trace CSV, aggregates counts by window
// and runs the author's algorithms' rebuild() to compute reconstruction metrics.
// Writes a CSV compatible with benchmark_results.csv used by visualize.py

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <set>

// Include the author's headers (already present in this project)
#include "Wavelet/wavelet.h"
#include "Fourier/fourier.h"
#include "OmniWindow/omniwindow.h"
#include "PersistCMS/persistCMS.h"

using namespace std;

// Minimal aliases to match author's types used elsewhere
using namespace std;

// Metrics (same formulas as in fattree_benchmark.cc)
double Euclidean(const vector<double>& a, const vector<double>& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) s += pow(a[i]-b[i],2);
    return sqrt(s);
}
double ARE(const vector<double>& a, const vector<double>& b) {
    double acc = 0.0; uint32_t nz = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] > 1e-9) { acc += fabs(a[i]-b[i]) / a[i]; nz++; }
    return (nz==0)?0.0:acc / nz;
}
double Cosine(const vector<double>& a, const vector<double>& b) {
    double dot=0, A=0, B=0;
    for (size_t i=0;i<a.size();++i){ dot += a[i]*b[i]; A += a[i]*a[i]; B += b[i]*b[i]; }
    A = sqrt(A); B = sqrt(B); if (A<1e-9 || B<1e-9) return 1.0; return dot/(A*B);
}
double EnergySim(const vector<double>& a, const vector<double>& b) {
    double ea=0, eb=0; for (size_t i=0;i<a.size();++i){ ea += a[i]*a[i]; eb += b[i]*b[i]; }
    if (ea < 1e-9) return (eb < 1e-9) ? 1.0 : 0.0;
    double r = eb/ea; return (r>1.0) ? 1.0/r : r;
}

// A forgiving CSV parser: tries to extract first two numeric columns as time and flowId
bool parseLine(const string &line, double &time_s, uint64_t &flowId) {
    if (line.empty()) return false;
    stringstream ss(line);
    string tok;
    vector<string> cols;
    while (getline(ss, tok, ',')) cols.push_back(tok);
    if (cols.size() < 2) return false;
    // Heuristic-aware parsing for common formats in websearch25.csv
    // Known layout (uMon WaveSketch dataset): <flowId>,<pktSize>,<timestamp_us>,...
    if (cols.size() >= 3) {
        // try to parse flowId from first column and time from third (microseconds)
        try {
            flowId = stoull(cols[0]);
            // timestamp in file is typically in microseconds (large integer)
            uint64_t tus = stoull(cols[2]);
            time_s = static_cast<double>(tus) / 1e6;
            return true;
        } catch(...) {
            // fall through to generic parsing below
        }
    }

    // Generic fallback: try to parse time from first or second column (seconds)
    bool timeParsed = false;
    try { time_s = stod(cols[0]); timeParsed = true; } catch(...) {}
    if (!timeParsed && cols.size() >= 2) {
        try { time_s = stod(cols[1]); timeParsed = true; } catch(...) {}
    }
    if (!timeParsed) return false;

    // Generic fallback for flow id: try second or third column, otherwise hash the whole line
    bool idParsed = false;
    if (cols.size() >= 2) {
        try { flowId = stoull(cols[1]); idParsed = true; } catch(...){}
    }
    if (!idParsed && cols.size() >= 3) {
        try { flowId = stoull(cols[2]); idParsed = true; } catch(...){}
    }
    if (!idParsed) { std::hash<string> h; flowId = h(line) & 0xffffffff; }
    return true;
}

int main(int argc, char** argv) {
    string input = "../uMon-WaveSketch/data/websearch25.csv";
    string output = "benchmark_results_offline.csv";
    uint32_t windowUs = 1000000; // default 1s windows
    string memoriesStr = "64,128"; // default memory budgets (KB)
    // optional inspect flows (comma separated list of flow ids)
    set<uint64_t> inspectFlows;
    bool perPacket = false;
    bool streaming = false;

    if (argc > 1) input = argv[1];
    for (int i=2;i<argc;i++) {
        string a = argv[i];
        if (a.rfind("--windowUs=",0)==0) windowUs = stoi(a.substr(11));
        else if (a.rfind("--memories=",0)==0) memoriesStr = a.substr(11);
        else if (a.rfind("--output=",0)==0) output = a.substr(9);
        else if (a.rfind("--inspect=",0)==0) {
            string v = a.substr(10);
            stringstream ss(v); string tok;
            while (getline(ss, tok, ',')) {
                try { inspectFlows.insert(stoull(tok)); } catch(...){}
            }
        } else if (a == "--per-packet") {
            perPacket = true;
        } else if (a == "--streaming") {
            // streaming mode: feed sketches directly while reading the CSV
            // avoids expanding whole dataset into memory
            streaming = true;
        }
    }

    vector<uint32_t> memories;
    stringstream ms(memoriesStr); string mtok;
    while (getline(ms, mtok, ',')) { try{ memories.push_back(stoi(mtok)); } catch(...){} }
    if (memories.empty()) memories.push_back(64);

    cerr << "Offline evaluator: input="<<input<<" windowUs="<<windowUs<<" memories=";
    for (auto v:memories) cerr<<v<<","; cerr<<" output="<<output<<"\n";

    ifstream ifs(input);
    if (!ifs.is_open()) { cerr<<"Failed to open input file: "<<input<<"\n"; return 1; }

    // map flowId -> (windowIndex -> count)
    map<uint64_t, map<uint32_t,uint32_t>> flowData;

    string line; size_t lines=0;
    while (getline(ifs, line)) {
        double tsec; uint64_t fid;
        if (!parseLine(line, tsec, fid)) continue;
        // convert to microseconds
        uint64_t tus = (uint64_t) llround(tsec * 1e6);
        uint32_t w = tus / windowUs;
        flowData[fid][w]++;
        ++lines;
    }
    cerr<<"Parsed "<<lines<<" lines, found "<<flowData.size()<<" flows\n";

    ofstream ofs(output, ios::app);
    if (!ofs.is_open()) { cerr<<"Failed to open output file: "<<output<<"\n"; return 1; }
    // write header if empty
    if (ofs.tellp() == 0) ofs << "time_s,algorithm,memory_kb,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist,energy_sim\n";

    ofstream inspectOut("inspect_flows.txt", ios::app);
    if (!inspectOut.is_open()) {
        cerr << "Warning: unable to open inspect_flows.txt for append\n";
    }

    // Build STREAM dict once (same for all algorithms)
    STREAM dict;
    for (auto &fentry : flowData) {
        STREAM_QUEUE q;
        for (auto &we : fentry.second) {
            uint32_t win = we.first;
            TIME tt = static_cast<TIME>(win * (uint64_t)windowUs);
            q.emplace_back(tt, static_cast<DATA>(we.second));
        }
        if (!q.empty()) dict[five_tuple((uint32_t)fentry.first)] = q;
    }

    // For each algorithm and memory budget, call rebuild() and compute metrics
    vector<string> algNames = {"wavesketch-ideal","fourier","omniwindow","persistcms"};
    for (auto mem : memories) {
        uint32_t memoryBytes = mem * 1024;
        uint32_t k = memoryBytes / 12;

        // instantiate algorithms
        wavelet<false> walg; walg.reset();
        fourier falg; falg.reset();
        omniwindow oalg; oalg.reset();
        persistCMS palg; palg.reset();

        // run each
        for (auto &alg : algNames) {
            // Before calling rebuild() we must feed the sketch-based algorithms
            // with the same counts they would see online via count(). If we
            // call rebuild() on a fresh object without any prior count() calls
            // the reconstructed stream will be empty (estimates==0).
            auto feedCounts = [&](abstract_scheme &s) {
                // ensure scheme is reset before feeding events to avoid stale start_time
                s.reset();

                if (streaming) {
                    // Stream directly from input file, emitting count(...,1) per packet.
                    // This avoids expanding everything to memory. Assumes input is
                    // approximately time-ordered; out-of-order streaming may break
                    // schemes that require monotonic time.
                    ifstream ifs2(input);
                    if (!ifs2.is_open()) { cerr<<"[streaming] Failed to re-open input: "<<input<<"\n"; return; }
                    string l; size_t n=0;
                    while (getline(ifs2, l)) {
                        double tsec; uint64_t fid;
                        if (!parseLine(l, tsec, fid)) continue;
                        TIME tt = static_cast<TIME>( (uint64_t) llround(tsec * 1e6) );
                        five_tuple ft((uint32_t)fid);
                        s.count(ft, tt, static_cast<DATA>(1));
                        ++n;
                        // occasional yield to keep system responsive for very large inputs
                        if ((n & 0xffff) == 0) {
                            // no-op; placeholder if we want to poll signals
                        }
                    }
                    s.flush();
                    return;
                }

                // non-streaming: build a global list of events (time, flowId, count)
                struct Ev { TIME t; uint64_t fid; DATA c; };
                std::vector<Ev> events;
                events.reserve(1024);
                for (auto &fentry2 : flowData) {
                    for (auto &we2 : fentry2.second) {
                        uint32_t win2 = we2.first;
                        TIME tt2 = static_cast<TIME>(win2 * (uint64_t)windowUs);
                        events.push_back({tt2, fentry2.first, static_cast<DATA>(we2.second)});
                    }
                }
                sort(events.begin(), events.end(), [](const Ev &a, const Ev &b){ return a.t < b.t; });
                // Expand (if requested) into a full list of per-packet events, then sort globally
                struct Pkt { TIME t; uint64_t fid; DATA c; };
                std::vector<Pkt> expanded;
                expanded.reserve(events.size() * 4);
                for (auto &e : events) {
                    if (perPacket && e.c > 0) {
                        uint32_t cnt = static_cast<uint32_t>(e.c);
                        uint64_t winStart = static_cast<uint64_t>(e.t);
                        uint64_t step = (cnt > 0) ? (windowUs / (cnt + 1)) : windowUs;
                        for (uint32_t i = 0; i < cnt; ++i) {
                            TIME ti = static_cast<TIME>(winStart + (uint64_t)(i + 1) * step);
                            expanded.push_back({ti, e.fid, static_cast<DATA>(1)});
                        }
                    } else {
                        expanded.push_back({e.t, e.fid, e.c});
                    }
                }
                sort(expanded.begin(), expanded.end(), [](const Pkt &a, const Pkt &b){ return a.t < b.t; });
                for (auto &p : expanded) {
                    five_tuple ft((uint32_t)p.fid);
                    s.count(ft, p.t, p.c);
                }
                s.flush();
            };

            STREAM reconstructed;
            if (alg == "wavesketch-ideal") {
                feedCounts(walg);
                reconstructed = walg.rebuild(dict);
            } else if (alg == "fourier") {
                feedCounts(falg);
                reconstructed = falg.rebuild(dict);
            } else if (alg == "omniwindow") {
                feedCounts(oalg);
                reconstructed = oalg.rebuild(dict);
            } else if (alg == "persistcms") {
                feedCounts(palg);
                reconstructed = palg.rebuild(dict);
            }

            // for each flow produce metrics
            for (auto &fentry : flowData) {
                uint64_t flowId = fentry.first;
                // determine number of windows from data (min..max)
                auto &wmap = fentry.second;
                if (wmap.empty()) continue;
                uint32_t start = wmap.begin()->first;
                uint32_t end = wmap.rbegin()->first;
                uint32_t numWindows = end - start + 1;
                vector<double> orig(numWindows, 0.0), rec(numWindows, 0.0);
                for (auto &we : wmap) { orig[we.first - start] = we.second; }
                // fill rec from reconstructed map
                auto itRec = reconstructed.find(five_tuple((uint32_t)flowId));
                if (itRec != reconstructed.end()) {
                    for (auto &p : itRec->second) {
                        TIME tt = p.first;
                        uint32_t win = tt / windowUs;
                        if (win >= start && win <= end) rec[win - start] += p.second;
                    }
                }

                double totalPackets = accumulate(orig.begin(), orig.end(), 0.0);
                double euc = Euclidean(orig, rec);
                double are = ARE(orig, rec);
                double cos = Cosine(orig, rec);
                double es = EnergySim(orig, rec);

                // If this flow is in the inspect set, print a compact comparison
                if (!inspectFlows.empty() && inspectFlows.count(flowId)) {
                    cerr << "--- Inspect flow="<<flowId<<" alg="<<alg<<" mem="<<mem<<" start_win="<<start<<" numWindows="<<numWindows<<" packets="<<totalPackets<<" ---\n";
                    if (inspectOut.is_open()) inspectOut << "--- Inspect flow="<<flowId<<" alg="<<alg<<" mem="<<mem<<" start_win="<<start<<" numWindows="<<numWindows<<" packets="<<totalPackets<<" ---\n";
                    cerr << "orig: "; if (inspectOut.is_open()) inspectOut << "orig: ";
                    for (size_t ii=0; ii<orig.size(); ++ii) { cerr<< (uint64_t)orig[ii] <<" "; if (inspectOut.is_open()) inspectOut<< (uint64_t)orig[ii] <<" "; }
                    cerr << "\nrec : "; if (inspectOut.is_open()) inspectOut << "\nrec : ";
                    for (size_t ii=0; ii<rec.size(); ++ii) { cerr<< (uint64_t)rec[ii] <<" "; if (inspectOut.is_open()) inspectOut<< (uint64_t)rec[ii] <<" "; }
                    cerr << "\nARE="<<are<<" COS="<<cos<<" Euc="<<euc<<" ES="<<es<<"\n";
                    if (inspectOut.is_open()) { inspectOut << "\nARE="<<are<<" COS="<<cos<<" Euc="<<euc<<" ES="<<es<<"\n"; inspectOut.flush(); }
                }

                ofs << 0.0 << "," // time_s placeholder - offline
                    << alg << ","
                    << mem << ","
                    << flowId << ","
                    << k << ","
                    << windowUs << ","
                    << totalPackets << ","
                    << are << ","
                    << cos << ","
                    << euc << ","
                    << es << "\n";
            }
            ofs.flush();
            cerr << "Done algorithm="<<alg<<" mem="<<mem<<"\n";
        }
    }

    cerr<<"Offline evaluation finished. Results appended to "<<output<<"\n";
    return 0;
}
