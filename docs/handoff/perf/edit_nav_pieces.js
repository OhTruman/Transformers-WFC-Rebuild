// 300 fps sim plan (campaign matinee ends, Escalation doors): BotNav::computePieces builds a flat (CSR) adjacency in reused buffers
// instead of a vector per cell each call. The connected pieces are a property of the graph, and piece ids still go to the
// lowest-index open seed in order with the main piece the first largest, so piece_, mainPieceId_ and mainPiece_ are unchanged
// (traversal order inside a piece is not observable). Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/BotNav.h', [
    ['    void computePieces();' + NL,
     '    void computePieces();' + NL + '    std::vector<int> adjStart_, adjList_, adjFill_, pieceStack_;   // computePieces scratch (CSR adjacency), reused' + NL],
]);
ed('src/game/BotNav.cpp', [
    ['    piece_.assign(cells_.size(), -1);' + NL +
     '    std::vector<std::vector<int>> adj(cells_.size());' + NL +
     '    for (size_t i = 0; i < cells_.size(); ++i) for (const Portal& p : cells_[i].portals) { adj[i].push_back(p.to); adj[(size_t)p.to].push_back((int)i); }' + NL +
     '    for (const Link& l : links_) { adj[(size_t)l.from].push_back(l.to); adj[(size_t)l.to].push_back(l.from); }' + NL,
     '    piece_.assign(cells_.size(), -1);' + NL +
     '    // The undirected adjacency (portals + links) as one flat array: degrees, prefix sums, then the neighbours.' + NL +
     '    const size_t N = cells_.size();' + NL +
     '    adjStart_.assign(N + 1, 0);' + NL +
     '    for (size_t i = 0; i < N; ++i) for (const Portal& p : cells_[i].portals) { ++adjStart_[i + 1]; ++adjStart_[(size_t)p.to + 1]; }' + NL +
     '    for (const Link& l : links_) { ++adjStart_[(size_t)l.from + 1]; ++adjStart_[(size_t)l.to + 1]; }' + NL +
     '    for (size_t i = 0; i < N; ++i) adjStart_[i + 1] += adjStart_[i];' + NL +
     '    adjList_.resize((size_t)adjStart_[N]);' + NL +
     '    adjFill_.assign(adjStart_.begin(), adjStart_.end() - 1);' + NL +
     '    for (size_t i = 0; i < N; ++i) for (const Portal& p : cells_[i].portals) { adjList_[(size_t)adjFill_[i]++] = p.to; adjList_[(size_t)adjFill_[(size_t)p.to]++] = (int)i; }' + NL +
     '    for (const Link& l : links_) { adjList_[(size_t)adjFill_[(size_t)l.from]++] = l.to; adjList_[(size_t)adjFill_[(size_t)l.to]++] = l.from; }' + NL],
    ['        int n = 0; std::vector<int> st{(int)i}; piece_[i] = pid;' + NL +
     '        while (!st.empty()) { int c = st.back(); st.pop_back(); ++n; for (int q : adj[(size_t)c]) if (piece_[(size_t)q] < 0 && open((size_t)q)) { piece_[(size_t)q] = pid; st.push_back(q); } }' + NL,
     '        int n = 0; std::vector<int>& st = pieceStack_; st.clear(); st.push_back((int)i); piece_[i] = pid;' + NL +
     '        while (!st.empty()) {' + NL +
     '            const int c = st.back(); st.pop_back(); ++n;' + NL +
     '            for (int k = adjStart_[(size_t)c]; k < adjStart_[(size_t)c + 1]; ++k) { const int q = adjList_[(size_t)k]; if (piece_[(size_t)q] < 0 && open((size_t)q)) { piece_[(size_t)q] = pid; st.push_back(q); } }' + NL +
     '        }' + NL],
]);
console.log('ok');
