#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <utility>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <initializer_list>

#include "../chess.hpp"

using namespace chess;
using namespace std;

constexpr int MATE    = 32000;
constexpr int INF     = 32001;
constexpr int MAX_PLY = 64;

enum : uint8_t { TT_NONE = 0, TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    uint64_t key   = 0;
    int16_t  score = 0;
    Move     move  = Move::NO_MOVE;
    uint8_t  depth = 0;
    uint8_t  flag  = TT_NONE;
};

static inline int pieceValue(PieceType pt) {
    if (pt == PieceType::PAWN)   return 100;
    if (pt == PieceType::KNIGHT) return 320;
    if (pt == PieceType::BISHOP) return 330;
    if (pt == PieceType::ROOK)   return 500;
    if (pt == PieceType::QUEEN)  return 900;
    if (pt == PieceType::KING)   return 20000;
    return 0;
}

struct Searcher {
    vector<TTEntry> tt;
    uint64_t ttMask;
    uint64_t nodes = 0;
    Move pvTable[MAX_PLY][MAX_PLY];
    int  pvLen[MAX_PLY];

    explicit Searcher(unsigned bits) {
        tt.assign(static_cast<size_t>(1) << bits, TTEntry{});
        ttMask = (static_cast<uint64_t>(1) << bits) - 1;
    }

    static inline int toTT(int s, int ply) {
        if (s >=  MATE - MAX_PLY) return s + ply;
        if (s <= -MATE + MAX_PLY) return s - ply;
        return s;
    }
    static inline int fromTT(int s, int ply) {
        if (s >=  MATE - MAX_PLY) return s - ply;
        if (s <= -MATE + MAX_PLY) return s + ply;
        return s;
    }

    static int evaluate(const Board& b) {
        int sc = 0;
        sc += 100 * ((int)b.pieces(PieceType::PAWN,   Color::WHITE).count() - (int)b.pieces(PieceType::PAWN,   Color::BLACK).count());
        sc += 320 * ((int)b.pieces(PieceType::KNIGHT, Color::WHITE).count() - (int)b.pieces(PieceType::KNIGHT, Color::BLACK).count());
        sc += 330 * ((int)b.pieces(PieceType::BISHOP, Color::WHITE).count() - (int)b.pieces(PieceType::BISHOP, Color::BLACK).count());
        sc += 500 * ((int)b.pieces(PieceType::ROOK,   Color::WHITE).count() - (int)b.pieces(PieceType::ROOK,   Color::BLACK).count());
        sc += 900 * ((int)b.pieces(PieceType::QUEEN,  Color::WHITE).count() - (int)b.pieces(PieceType::QUEEN,  Color::BLACK).count());
        return b.sideToMove() == Color::WHITE ? sc : -sc;
    }

    static inline int scoreMove(const Board& b, Move m, Move ttMove) {
        if (m == ttMove) return 1000000;
        int s = 0;
        bool ep  = m.typeOf() == Move::ENPASSANT;
        bool cap = ep || b.at(m.to()).type() != PieceType::NONE;
        if (cap) {
            PieceType victim   = ep ? PieceType::PAWN : b.at(m.to()).type();
            PieceType attacker = b.at(m.from()).type();
            s += 100000 + pieceValue(victim) * 16 - pieceValue(attacker);
        }
        if (m.typeOf() == Move::PROMOTION) s += 90000 + pieceValue(m.promotionType());
        return s;
    }

    int search(Board& b, int depth, int ply, int alpha, int beta) {
        ++nodes;
        pvLen[ply] = 0;

        if (ply > 0) {
            alpha = max(alpha, -MATE + ply);
            beta  = min(beta,   MATE - ply - 1);
            if (alpha >= beta) return alpha;
        }

        const uint64_t key = b.hash();
        TTEntry& e = tt[key & ttMask];
        Move ttMove = Move::NO_MOVE;
        if (e.key == key) {
            ttMove = e.move;
            if (ply > 0 && e.depth >= depth) {
                int s = fromTT(e.score, ply);
                if (e.flag == TT_EXACT) return s;
                if (e.flag == TT_LOWER && s >= beta)  return s;
                if (e.flag == TT_UPPER && s <= alpha) return s;
            }
        }

        Movelist moves;
        movegen::legalmoves(moves, b);
        if (moves.empty()) return b.inCheck() ? -MATE + ply : 0;
        if (depth <= 0) return evaluate(b);

        const int n = moves.size();
        int scores[256];
        for (int i = 0; i < n; ++i) scores[i] = scoreMove(b, moves[i], ttMove);

        int  bestScore = -INF;
        Move bestMove  = moves[0];
        uint8_t flag   = TT_UPPER;

        for (int i = 0; i < n; ++i) {
            int best = i;
            for (int j = i + 1; j < n; ++j) if (scores[j] > scores[best]) best = j;
            if (best != i) { swap(moves[best], moves[i]); swap(scores[best], scores[i]); }

            Move m = moves[i];
            b.makeMove(m);
            int score = -search(b, depth - 1, ply + 1, -beta, -alpha);
            b.unmakeMove(m);

            if (score > bestScore) {
                bestScore = score;
                bestMove  = m;
                if (score > alpha) {
                    alpha = score;
                    flag  = TT_EXACT;
                    pvTable[ply][0] = m;
                    for (int k = 0; k < pvLen[ply + 1]; ++k) pvTable[ply][k + 1] = pvTable[ply + 1][k];
                    pvLen[ply] = pvLen[ply + 1] + 1;
                    if (alpha >= beta) { flag = TT_LOWER; break; }
                }
            }
        }

        if (e.key != key || depth >= e.depth || flag == TT_EXACT) {
            e.key   = key;
            e.score = (int16_t)toTT(bestScore, ply);
            e.move  = bestMove;
            e.depth = (uint8_t)max(0, depth);
            e.flag  = flag;
        }
        return bestScore;
    }
};

struct Result {
    Move best = Move::NO_MOVE;
    int  score = 0;
    string pv;
    uint64_t nodes = 0;
    double ms = 0.0;
};

static Result solveFen(const string& fen, int maxDepth, Searcher& s) {
    Board b(fen);
    s.nodes = 0;
    Result r;
    auto t0 = chrono::high_resolution_clock::now();

    int score = 0;
    for (int d = 1; d <= maxDepth; ++d) {
        score = s.search(b, d, 0, -INF, INF);
        r.best = s.pvTable[0][0];
        if (score >= MATE - MAX_PLY) break;
    }

    auto t1 = chrono::high_resolution_clock::now();
    r.ms    = chrono::duration<double, milli>(t1 - t0).count();
    r.score = score;
    r.nodes = s.nodes;

    string pv;
    for (int i = 0; i < s.pvLen[0]; ++i) {
        if (!pv.empty()) pv += ' ';
        pv += uci::moveToUci(s.pvTable[0][i]);
    }
    r.pv = pv;
    return r;
}

namespace mini {
struct Value {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double num = 0;
    string str;
    vector<Value> arr;
    vector<pair<string, Value>> obj;
};
struct Parser {
    const char* p;
    const char* end;
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    Value parse() { return value(); }
    Value value() {
        ws();
        if (p >= end) return {};
        char c = *p;
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') { Value v; v.t = Value::Str; v.str = string_(); return v; }
        if (c == 't' || c == 'f') return boolean();
        if (c == 'n') { p += (4 <= (end - p) ? 4 : (end - p)); Value v; v.t = Value::Null; return v; }
        return number();
    }
    string string_() {
        string s;
        if (p < end && *p == '"') ++p;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\' && p < end) {
                char e = *p++;
                switch (e) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case '/': s += '/';  break;
                    case '\\': s += '\\'; break;
                    case '"': s += '"';  break;
                    case 'u':
                        if (p + 4 <= end) {
                            int code = (int)strtol(string(p, p + 4).c_str(), nullptr, 16);
                            p += 4;
                            if (code < 128) s += (char)code;
                        }
                        break;
                    default: s += e;
                }
            } else s += c;
        }
        if (p < end) ++p;
        return s;
    }
    Value number() {
        const char* s = p;
        while (p < end && (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) ++p;
        Value v; v.t = Value::Num; v.num = strtod(string(s, p).c_str(), nullptr);
        return v;
    }
    Value boolean() {
        Value v; v.t = Value::Bool;
        if (*p == 't') { v.b = true;  p += 4; } else { v.b = false; p += 5; }
        return v;
    }
    Value array() {
        Value v; v.t = Value::Arr; ++p; ws();
        if (p < end && *p == ']') { ++p; return v; }
        while (p < end) {
            v.arr.push_back(value()); ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; break; }
            break;
        }
        return v;
    }
    Value object() {
        Value v; v.t = Value::Obj; ++p; ws();
        if (p < end && *p == '}') { ++p; return v; }
        while (p < end) {
            ws();
            string k = string_();
            ws();
            if (p < end && *p == ':') ++p;
            Value val = value();
            v.obj.emplace_back(move(k), move(val));
            ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; break; }
            break;
        }
        return v;
    }
};
}

static string getField(const mini::Value& v, initializer_list<const char*> keys) {
    if (v.t != mini::Value::Obj) return "";
    for (const char* k : keys)
        for (const auto& kv : v.obj)
            if (kv.first == k && kv.second.t == mini::Value::Str) return kv.second.str;
    return "";
}

static string asSolution(const mini::Value& v) {
    if (v.t == mini::Value::Str) return v.str;
    if (v.t == mini::Value::Arr) {
        string out;
        for (const auto& e : v.arr)
            if (e.t == mini::Value::Str) { if (!out.empty()) out += ' '; out += e.str; }
        return out;
    }
    if (v.t == mini::Value::Obj) {
        for (const char* k : {"moves", "solution", "pv", "best", "bestmove", "line"})
            for (const auto& kv : v.obj)
                if (kv.first == k) return asSolution(kv.second);
    }
    return "";
}

static void collect(const mini::Value& v, vector<pair<string, string>>& out) {
    if (v.t == mini::Value::Obj) {
        string fen = getField(v, {"fen", "FEN", "epd", "position"});
        if (!fen.empty()) { out.emplace_back(fen, asSolution(v)); return; }
        bool anyFenKey = false;
        for (const auto& kv : v.obj) if (kv.first.find('/') != string::npos) { anyFenKey = true; break; }
        if (anyFenKey) { for (const auto& kv : v.obj) out.emplace_back(kv.first, asSolution(kv.second)); return; }
        for (const auto& kv : v.obj) collect(kv.second, out);
    } else if (v.t == mini::Value::Arr) {
        for (const auto& e : v.arr) collect(e, out);
    }
}

static string stripMoveNumber(string tok) {
    size_t i = 0;
    while (i < tok.size() && isdigit((unsigned char)tok[i])) ++i;
    if (i > 0) {
        size_t j = i;
        while (j < tok.size() && tok[j] == '.') ++j;
        if (j > i) return tok.substr(j);
    }
    return tok;
}

static Move firstSolutionMove(const string& fen, const string& expected) {
    Board b(fen);
    istringstream is(expected);
    string raw;
    while (is >> raw) {
        string tok = stripMoveNumber(raw);
        if (tok.empty()) continue;
        if (tok == "1-0" || tok == "0-1" || tok == "1/2-1/2" || tok == "*") continue;
        Move em = Move::NO_MOVE;
        try { em = uci::uciToMove(b, tok); } catch (...) {}
        if (em == Move::NO_MOVE) { try { em = uci::parseSan(b, tok); } catch (...) {} }
        return em;
    }
    return Move::NO_MOVE;
}

int main(int argc, char** argv) {
    string path     = argc > 1 ? argv[1] : "mate_in_4.json";
    int maxDepth     = argc > 2 ? atoi(argv[2]) : 9;
    unsigned ttBits  = argc > 3 ? (unsigned)atoi(argv[3]) : 22;

    ifstream f(path, ios::binary);
    if (!f) { cerr << "Cannot open " << path << "\n"; return 1; }
    stringstream ss; ss << f.rdbuf();
    string data = ss.str();

    mini::Parser parser{data.data(), data.data() + data.size()};
    mini::Value root = parser.parse();
    vector<pair<string, string>> pairs;
    collect(root, pairs);

    if (pairs.empty()) { cerr << "No (fen, solution) pairs extracted.\n"; return 1; }

    Searcher s(ttBits);
    int solved = 0;
    double totalMs = 0.0;
    uint64_t totalNodes = 0;

    for (size_t i = 0; i < pairs.size(); ++i) {
        const string& fen = pairs[i].first;
        const string& exp = pairs[i].second;

        Result r = solveFen(fen, maxDepth, s);

        Board bb(fen);
        Move expMove = firstSolutionMove(fen, exp);
        bool match   = (expMove != Move::NO_MOVE) && (expMove == r.best);

        string bestSan;
        try { bestSan = uci::moveToSan(bb, r.best); } catch (...) { bestSan = uci::moveToUci(r.best); }

        if (match) ++solved;
        totalMs += r.ms;
        totalNodes += r.nodes;

        cout << "[" << (i + 1) << "/" << pairs.size() << "] "
             << (match ? "OK   " : "MISS ")
             << "best=" << bestSan << " (" << uci::moveToUci(r.best) << ")"
             << " score=" << r.score
             << " pv=" << r.pv
             << " exp=\"" << exp << "\""
             << " nodes=" << r.nodes
             << " time=" << r.ms << "ms\n";
    }

    cout << "----\nSolved " << solved << "/" << pairs.size()
         << "  total=" << totalMs << "ms"
         << "  avg=" << (totalMs / pairs.size()) << "ms"
         << "  nodes=" << totalNodes << "\n";
    return 0;
}