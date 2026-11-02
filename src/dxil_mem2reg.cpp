#include "downgrader/dxil_mem2reg.h"
#include <algorithm>
#include <iostream>
#include <functional>

namespace kepler {

// ----------------------------------------------------------------------------
// CFG Analysis & Dominance Engine
// ----------------------------------------------------------------------------

bool CFGAnalysis::Analyze(const DXILIntermediateRepresentation& ir, CFGAnalysis& outCFG) {
    outCFG = CFGAnalysis();
    outCFG.numBlocks = ir.numBasicBlocks;
    outCFG.entryBlock = 0;

    if (ir.basicBlocks.empty()) {
        return true;
    }

    uint32_t N = static_cast<uint32_t>(ir.basicBlocks.size());

    // 1. Initialize empty predecessors and successors
    for (uint32_t b = 0; b < N; ++b) {
        outCFG.preds[b] = {};
        outCFG.succs[b] = {};
    }

    // 2. Discover edges from block terminators and basic block metadata
    for (uint32_t b = 0; b < N; ++b) {
        const auto& bb = ir.basicBlocks[b];
        std::vector<uint32_t> rawSuccs;

        if (bb.hasTerminator) {
            switch (bb.terminator.kind) {
                case BlockTerminatorKind::UnconditionalBranch:
                    rawSuccs.push_back(bb.terminator.targetBlock);
                    break;
                case BlockTerminatorKind::ConditionalBranch:
                    rawSuccs.push_back(bb.terminator.trueBlock);
                    rawSuccs.push_back(bb.terminator.falseBlock);
                    break;
                case BlockTerminatorKind::Switch:
                    rawSuccs.push_back(bb.terminator.defaultBlock);
                    for (const auto& c : bb.terminator.switchCases) {
                        rawSuccs.push_back(c.second);
                    }
                    break;
                case BlockTerminatorKind::Return:
                case BlockTerminatorKind::Unreachable:
                case BlockTerminatorKind::None:
                    break;
            }
        } else if (!bb.successors.empty()) {
            rawSuccs = bb.successors;
        }

        // Deduplicate and validate bounds
        for (uint32_t s : rawSuccs) {
            if (s >= N) {
                outCFG.isConsistent = false;
                outCFG.diagnostic = "Block " + std::to_string(b) + " has out-of-range successor " + std::to_string(s) + ".";
                return false;
            }
            if (std::find(outCFG.succs[b].begin(), outCFG.succs[b].end(), s) == outCFG.succs[b].end()) {
                outCFG.succs[b].push_back(s);
            }
        }
    }

    // 3. Populate predecessors from successors
    for (uint32_t b = 0; b < N; ++b) {
        for (uint32_t s : outCFG.succs[b]) {
            outCFG.preds[s].push_back(b);
        }
    }

    // 4. Predecessor consistency verification
    for (uint32_t b = 0; b < N; ++b) {
        if (!ir.basicBlocks[b].predecessors.empty()) {
            for (uint32_t p : ir.basicBlocks[b].predecessors) {
                if (p >= N) {
                    outCFG.isConsistent = false;
                    outCFG.diagnostic = "Block " + std::to_string(b) + " lists out-of-range predecessor " + std::to_string(p) + ".";
                    return false;
                }
                const auto& sList = outCFG.succs[p];
                if (std::find(sList.begin(), sList.end(), b) == sList.end()) {
                    outCFG.isConsistent = false;
                    outCFG.diagnostic = "Predecessor/successor mismatch: Block " + std::to_string(b) + 
                                        " lists predecessor " + std::to_string(p) + ", but " + std::to_string(p) + 
                                        " does not list " + std::to_string(b) + " as successor.";
                    return false;
                }
            }
        }
    }

    // 5. Reachability Analysis (BFS from entry)
    outCFG.reachableBlocks.insert(outCFG.entryBlock);
    std::vector<uint32_t> queue = { outCFG.entryBlock };
    size_t qHead = 0;
    while (qHead < queue.size()) {
        uint32_t curr = queue[qHead++];
        for (uint32_t s : outCFG.succs[curr]) {
            if (outCFG.reachableBlocks.insert(s).second) {
                queue.push_back(s);
            }
        }
    }

    if (outCFG.reachableBlocks.size() > 16) {
        outCFG.exceedsBlockLimit = true;
    }

    // 6. Dominance Computation (Iterative Fixed Point for all reachable blocks)
    outCFG.dom[outCFG.entryBlock] = { outCFG.entryBlock };
    for (uint32_t b : outCFG.reachableBlocks) {
        if (b != outCFG.entryBlock) {
            outCFG.dom[b] = outCFG.reachableBlocks;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t b : outCFG.reachableBlocks) {
            if (b == outCFG.entryBlock) continue;

            std::set<uint32_t> inter;
            bool firstPred = true;
            for (uint32_t p : outCFG.preds[b]) {
                if (!outCFG.reachableBlocks.count(p)) continue;
                if (firstPred) {
                    inter = outCFG.dom[p];
                    firstPred = false;
                } else {
                    std::set<uint32_t> tmp;
                    std::set_intersection(inter.begin(), inter.end(),
                                          outCFG.dom[p].begin(), outCFG.dom[p].end(),
                                          std::inserter(tmp, tmp.begin()));
                    inter = std::move(tmp);
                }
            }
            inter.insert(b);
            if (inter != outCFG.dom[b]) {
                outCFG.dom[b] = std::move(inter);
                changed = true;
            }
        }
    }

    // 7. Immediate Dominators (idom) & Dominator Tree Children
    for (uint32_t b : outCFG.reachableBlocks) {
        if (b == outCFG.entryBlock) continue;
        std::set<uint32_t> strictDom = outCFG.dom[b];
        strictDom.erase(b);

        uint32_t idomVal = outCFG.entryBlock;
        bool foundIdom = false;
        for (uint32_t d : strictDom) {
            bool isClosest = true;
            for (uint32_t s : strictDom) {
                if (s != d && !outCFG.dom[d].count(s)) {
                    isClosest = false;
                    break;
                }
            }
            if (isClosest) {
                idomVal = d;
                foundIdom = true;
                break;
            }
        }
        if (foundIdom) {
            outCFG.idom[b] = idomVal;
            outCFG.domTreeChildren[idomVal].push_back(b);
        }
    }

    // 8. Dominance Frontier (DF)
    for (uint32_t b : outCFG.reachableBlocks) {
        outCFG.domFrontier[b] = {};
    }

    for (uint32_t x : outCFG.reachableBlocks) {
        if (outCFG.preds[x].size() >= 2) {
            auto itIdom = outCFG.idom.find(x);
            uint32_t idomX = (itIdom != outCFG.idom.end()) ? itIdom->second : outCFG.entryBlock;
            for (uint32_t p : outCFG.preds[x]) {
                if (!outCFG.reachableBlocks.count(p)) continue;
                uint32_t runner = p;
                while (runner != idomX && runner != x) {
                    outCFG.domFrontier[runner].insert(x);
                    if (outCFG.idom.find(runner) == outCFG.idom.end()) break;
                    runner = outCFG.idom[runner];
                }
            }
        }
    }

    // 9. Backedge Identification:
    // Identify backedges via DFS cycle traversal from entry, plus dominance backedges.
    std::vector<std::pair<uint32_t, uint32_t>> backedges;
    std::vector<bool> dfsVisited(N, false);
    std::vector<bool> onStack(N, false);
    std::function<void(uint32_t)> dfsFindBackEdges = [&](uint32_t u) {
        dfsVisited[u] = true;
        onStack[u] = true;
        for (uint32_t v : outCFG.succs[u]) {
            if (onStack[v]) {
                backedges.push_back({ u, v });
            } else if (!dfsVisited[v]) {
                dfsFindBackEdges(v);
            }
        }
        onStack[u] = false;
    };
    if (outCFG.entryBlock < N) {
        dfsFindBackEdges(outCFG.entryBlock);
    }
    for (uint32_t u : outCFG.reachableBlocks) {
        for (uint32_t v : outCFG.succs[u]) {
            if (outCFG.dom[u].count(v)) {
                bool exists = false;
                for (const auto& be : backedges) {
                    if (be.first == u && be.second == v) { exists = true; break; }
                }
                if (!exists) backedges.push_back({ u, v });
            }
        }
    }

    // 10. Reachability / Cycle Check via Floyd-Warshall
    std::vector<std::vector<bool>> canReach(N, std::vector<bool>(N, false));
    for (uint32_t u = 0; u < N; ++u) {
        for (uint32_t v : outCFG.succs[u]) {
            canReach[u][v] = true;
        }
    }
    for (uint32_t k = 0; k < N; ++k) {
        for (uint32_t i = 0; i < N; ++i) {
            for (uint32_t j = 0; j < N; ++j) {
                if (canReach[i][k] && canReach[k][j]) {
                    canReach[i][j] = true;
                }
            }
        }
    }

    bool hasAnyCycle = false;
    for (uint32_t b : outCFG.reachableBlocks) {
        if (canReach[b][b]) {
            hasAnyCycle = true;
            break;
        }
    }
    outCFG.hasCycle = hasAnyCycle;

    if (hasAnyCycle && backedges.empty()) {
        outCFG.isIrreducible = true;
        outCFG.diagnostic = "Irreducible control flow graph: directed cycle has no dominating header.";
    }

    // 11. Bounded Natural Loop Analysis (Section 3 & Section 5)
    outCFG.hasNaturalLoop = false;
    outCFG.loopInfo = NaturalLoopInfo();

    if (!backedges.empty()) {
        if (backedges.size() > 1) {
            outCFG.loopInfo.isNaturalLoop = false;
            outCFG.loopInfo.isCanonical = false;
            outCFG.loopInfo.rejectionReason = "Multiple backedges or nested loops detected (" + std::to_string(backedges.size()) + " backedges).";
        } else {
            uint32_t latch = backedges[0].first;
            uint32_t header = backedges[0].second;

            if (header == outCFG.entryBlock) {
                outCFG.loopInfo.isNaturalLoop = false;
                outCFG.loopInfo.isCanonical = false;
                outCFG.loopInfo.rejectionReason = "Loop header is entry block (no preheader).";
            } else {
                // Construct natural loop block set
                std::set<uint32_t> loopNodes = { header, latch };
                std::vector<uint32_t> wl;
                if (latch != header) wl.push_back(latch);
                while (!wl.empty()) {
                    uint32_t curr = wl.back();
                    wl.pop_back();
                    for (uint32_t p : outCFG.preds[curr]) {
                        if (p != header && canReach[header][p]) {
                            if (loopNodes.insert(p).second) {
                                wl.push_back(p);
                            }
                        }
                    }
                }

                if (loopNodes.size() > 6) {
                    outCFG.loopInfo.isNaturalLoop = false;
                    outCFG.loopInfo.isCanonical = false;
                    outCFG.loopInfo.rejectionReason = "Loop body exceeds bounded limit of 6 blocks (" + std::to_string(loopNodes.size()) + " blocks).";
                } else {
                    // Check single-entry: every block in loopNodes \ {header} must have all predecessors in loopNodes
                    bool sideEntry = false;
                    for (uint32_t b : loopNodes) {
                        if (b == header) continue;
                        for (uint32_t p : outCFG.preds[b]) {
                            if (!loopNodes.count(p)) {
                                sideEntry = true;
                                outCFG.loopInfo.isNaturalLoop = false;
                                outCFG.loopInfo.isCanonical = false;
                                outCFG.loopInfo.rejectionReason = "Side-entry into loop body block " + std::to_string(b) + " from block " + std::to_string(p) + ".";
                                break;
                            }
                        }
                        if (sideEntry) break;
                    }

                    if (!sideEntry) {
                        // Preheader identification: predecessors of header outside loopNodes
                        std::vector<uint32_t> outsidePreds;
                        for (uint32_t p : outCFG.preds[header]) {
                            if (!loopNodes.count(p)) {
                                outsidePreds.push_back(p);
                            }
                        }

                        if (outsidePreds.empty()) {
                            outCFG.loopInfo.isNaturalLoop = false;
                            outCFG.loopInfo.isCanonical = false;
                            outCFG.loopInfo.rejectionReason = "Loop header has no preheader.";
                        } else if (outsidePreds.size() > 1) {
                            outCFG.loopInfo.isNaturalLoop = false;
                            outCFG.loopInfo.isCanonical = false;
                            outCFG.loopInfo.rejectionReason = "Loop header has multiple preheaders (" + std::to_string(outsidePreds.size()) + ").";
                        } else {
                            uint32_t preheader = outsidePreds[0];

                            // Exiting and Exit Blocks
                            std::set<uint32_t> exitingBlocks;
                            std::set<uint32_t> exitBlocks;
                            for (uint32_t b : loopNodes) {
                                for (uint32_t s : outCFG.succs[b]) {
                                    if (!loopNodes.count(s)) {
                                        exitingBlocks.insert(b);
                                        exitBlocks.insert(s);
                                    }
                                }
                            }

                            if (exitBlocks.empty()) {
                                outCFG.loopInfo.isNaturalLoop = false;
                                outCFG.loopInfo.isCanonical = false;
                                outCFG.loopInfo.rejectionReason = "Loop has no exit block.";
                            } else if (exitingBlocks.size() > 1 || exitBlocks.size() > 1) {
                                outCFG.loopInfo.isNaturalLoop = false;
                                outCFG.loopInfo.isCanonical = false;
                                outCFG.loopInfo.rejectionReason = "Loop must have exactly one exiting block and one exit block (found " +
                                    std::to_string(exitingBlocks.size()) + " exiting, " + std::to_string(exitBlocks.size()) + " exit).";
                            } else {
                                // Secondary cycle check in subgraph without backedge
                                bool hasSecondaryCycle = false;
                                std::vector<std::vector<bool>> canReachNoBackedge(N, std::vector<bool>(N, false));
                                for (uint32_t u = 0; u < N; ++u) {
                                    for (uint32_t v : outCFG.succs[u]) {
                                        if (u == latch && v == header) continue;
                                        canReachNoBackedge[u][v] = true;
                                    }
                                }
                                for (uint32_t k = 0; k < N; ++k) {
                                    for (uint32_t i = 0; i < N; ++i) {
                                        for (uint32_t j = 0; j < N; ++j) {
                                            if (canReachNoBackedge[i][k] && canReachNoBackedge[k][j]) {
                                                canReachNoBackedge[i][j] = true;
                                            }
                                        }
                                    }
                                }
                                for (uint32_t b = 0; b < N; ++b) {
                                    if (canReachNoBackedge[b][b]) {
                                        hasSecondaryCycle = true;
                                        break;
                                    }
                                }

                                if (hasSecondaryCycle) {
                                    outCFG.loopInfo.isNaturalLoop = false;
                                    outCFG.loopInfo.isCanonical = false;
                                    outCFG.loopInfo.rejectionReason = "Secondary or nested cycle detected.";
                                } else {
                                    outCFG.loopInfo.isNaturalLoop = true;
                                    outCFG.loopInfo.isCanonical = true;
                                    outCFG.loopInfo.header = header;
                                    outCFG.loopInfo.preheader = preheader;
                                    outCFG.loopInfo.latch = latch;
                                    outCFG.loopInfo.exitingBlock = *exitingBlocks.begin();
                                    outCFG.loopInfo.exitBlock = *exitBlocks.begin();
                                    outCFG.loopInfo.backEdge = { latch, header };
                                    outCFG.loopInfo.loopBlocks = loopNodes;
                                    outCFG.hasNaturalLoop = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 12. Critical Edge Detection (Edge u -> v where succs[u].size() > 1 && preds[v].size() > 1)
    outCFG.criticalEdges.clear();
    for (uint32_t u : outCFG.reachableBlocks) {
        if (outCFG.succs[u].size() > 1) {
            for (uint32_t v : outCFG.succs[u]) {
                if (outCFG.preds[v].size() > 1) {
                    outCFG.criticalEdges.push_back({ u, v });
                }
            }
        }
    }

    return true;
}

bool CFGAnalysis::SplitCriticalEdge(
    DXILIntermediateRepresentation& ir,
    CFGAnalysis& cfg,
    uint32_t u,
    uint32_t v,
    uint32_t& outSplitBlock)
{
    if (ir.numBasicBlocks >= 16 || ir.basicBlocks.size() >= 16) {
        return false;
    }
    if (u >= ir.basicBlocks.size() || v >= ir.basicBlocks.size()) {
        return false;
    }

    uint32_t splitId = static_cast<uint32_t>(ir.basicBlocks.size());
    DXILBasicBlock splitBB;
    splitBB.id = splitId;
    splitBB.predecessors.push_back(u);
    splitBB.successors.push_back(v);
    splitBB.terminator.kind = BlockTerminatorKind::UnconditionalBranch;
    splitBB.terminator.targetBlock = v;
    splitBB.hasTerminator = true;
    ir.basicBlocks.push_back(splitBB);
    ir.numBasicBlocks = static_cast<uint32_t>(ir.basicBlocks.size());

    // Update u's terminator that pointed to v
    auto& uTerm = ir.basicBlocks[u].terminator;
    if (uTerm.kind == BlockTerminatorKind::ConditionalBranch) {
        if (uTerm.trueBlock == v) uTerm.trueBlock = splitId;
        else if (uTerm.falseBlock == v) uTerm.falseBlock = splitId;
    } else if (uTerm.kind == BlockTerminatorKind::UnconditionalBranch) {
        if (uTerm.targetBlock == v) uTerm.targetBlock = splitId;
    }

    // Update u's successors in ir.basicBlocks[u]
    for (auto& s : ir.basicBlocks[u].successors) {
        if (s == v) {
            s = splitId;
            break;
        }
    }

    // Update v's predecessors in ir.basicBlocks[v]
    for (auto& p : ir.basicBlocks[v].predecessors) {
        if (p == u) {
            p = splitId;
            break;
        }
    }

    // Update any pre-existing PHI nodes in v that refer to u
    for (auto& phi : ir.basicBlocks[v].phiNodes) {
        for (auto& inc : phi.incoming) {
            if (inc.predecessorBlock == u) {
                inc.predecessorBlock = splitId;
            }
        }
    }

    // Recompute CFG analysis
    Analyze(ir, cfg);
    outSplitBlock = splitId;
    return true;
}

std::set<uint32_t> CFGAnalysis::ComputeIDF(const CFGAnalysis& cfg, const std::set<uint32_t>& defBlocks) {
    std::set<uint32_t> idf;
    std::vector<uint32_t> worklist(defBlocks.begin(), defBlocks.end());
    std::set<uint32_t> inWorklist = defBlocks;

    while (!worklist.empty()) {
        uint32_t b = worklist.back();
        worklist.pop_back();

        auto it = cfg.domFrontier.find(b);
        if (it != cfg.domFrontier.end()) {
            for (uint32_t dfBlock : it->second) {
                if (idf.insert(dfBlock).second) {
                    if (inWorklist.insert(dfBlock).second) {
                        worklist.push_back(dfBlock);
                    }
                }
            }
        }
    }
    return idf;
}

// ----------------------------------------------------------------------------
// Eligibility Analysis
// ----------------------------------------------------------------------------

bool DXILMem2Reg::IsEligible(
    const DXILIntermediateRepresentation& ir,
    uint32_t slotIdx,
    std::string* pReason)
{
    CFGAnalysis cfg;
    CFGAnalysis::Analyze(ir, cfg);
    return IsEligibleWithCFG(ir, cfg, slotIdx, pReason);
}

bool DXILMem2Reg::IsEligibleWithCFG(
    const DXILIntermediateRepresentation& ir,
    const CFGAnalysis& cfg,
    uint32_t slotIdx,
    std::string* pReason)
{
    if (slotIdx >= ir.localAllocs.size()) {
        if (pReason) *pReason = "Slot index out of bounds.";
        return false;
    }

    const auto& alloc = ir.localAllocs[slotIdx];

    // Rule 1: Fixed-size, scalar allocation only
    if (alloc.numElements != 1 || alloc.isArray || alloc.isVector) {
        if (pReason) *pReason = "Allocation is aggregate, array, or vector (numElements=" + 
                                std::to_string(alloc.numElements) + ").";
        return false;
    }

    // Rule 2: 32-bit Float or 32-bit Integer only
    if (alloc.elemKind != LLVMTypeKind::Float && alloc.elemKind != LLVMTypeKind::Integer) {
        if (pReason) *pReason = "Allocation element type is not 32-bit float or integer.";
        return false;
    }

    // Rule 2.5: CFG Cycle & Non-Canonical Loop Rejection
    if (cfg.hasCycle) {
        if (!cfg.hasNaturalLoop || !cfg.loopInfo.isCanonical) {
            if (pReason) *pReason = "Control flow contains unsupported cycles/loops (" + cfg.loopInfo.rejectionReason + "); multi-block promotion ineligible.";
            return false;
        }
    }

    // Rule 3: Discover memory operations and containing basic blocks
    std::set<uint32_t> blocksWithUses;
    std::set<uint32_t> storeBlocks;
    std::set<uint32_t> loadBlocks;
    uint32_t storeCount = 0;
    uint32_t loadCount = 0;

    for (uint32_t b = 0; b < ir.numBasicBlocks; ++b) {
        if (b >= ir.basicBlocks.size()) break;
        for (const auto& op : ir.basicBlocks[b].instructions) {
            if (op.isLocalMemOp && op.localSlot == alloc.slotIndex) {
                blocksWithUses.insert(b);
                if (op.isLocalStore) {
                    storeCount++;
                    storeBlocks.insert(b);
                } else {
                    loadCount++;
                    loadBlocks.insert(b);
                }
            }
        }
    }

    if (blocksWithUses.empty()) {
        if (pReason) *pReason = "Allocation has no recorded memory uses.";
        return false;
    }

    // Rule 4: Pointer Provenance, Dynamic Indexing & Pointer Escapes
    for (const auto& ptrPair : ir.localPointers) {
        if (ptrPair.second.slotIndex == alloc.slotIndex) {
            if (ptrPair.second.isEscaped || !ptrPair.second.isValid) {
                if (pReason) *pReason = "Pointer to local storage escapes or has invalid provenance.";
                return false;
            }
            if (ptrPair.second.isDynamicIndex) {
                if (pReason) *pReason = "Dynamic indexing in pointer reference is unsupported for promotion.";
                return false;
            }
            if (ptrPair.second.subSlotIndex != 0) {
                if (pReason) *pReason = "Pointer has non-zero sub-slot or invalid provenance.";
                return false;
            }
        }
    }

    // Check if the allocation's SSA pointer escapes into non-memory instructions
    for (uint32_t b = 0; b < ir.numBasicBlocks; ++b) {
        if (b >= ir.basicBlocks.size()) break;
        for (const auto& op : ir.basicBlocks[b].instructions) {
            if (!op.isLocalMemOp) {
                for (int c = 0; c < 4; ++c) {
                    if (op.src0[c].ssaId == alloc.ssaPointer || op.src1[c].ssaId == alloc.ssaPointer) {
                        if (pReason) *pReason = "Pointer escapes into non-memory instruction operand.";
                        return false;
                    }
                }
            }
        }
    }
    for (const auto& pair : ir.comparisons) {
        if (pair.second.src0.ssaId == alloc.ssaPointer || pair.second.src1.ssaId == alloc.ssaPointer) {
            if (pReason) *pReason = "Pointer escapes into comparison instruction operand.";
            return false;
        }
    }

    // Rule 5: Loop Containment & Classification (Section 3, 4, 5, 9)
    if (ir.hasLoop && (!cfg.hasNaturalLoop || !cfg.loopInfo.isCanonical)) {
        for (uint32_t b : blocksWithUses) {
            if (ir.loop.loopBlocks.count(b)) {
                if (pReason) *pReason = "Allocation participates in loop body; multi-block promotion ineligible.";
                return false;
            }
        }
    }

    bool participatesInLoop = false;
    if (cfg.hasNaturalLoop && cfg.loopInfo.isCanonical) {
        for (uint32_t b : blocksWithUses) {
            if (cfg.loopInfo.loopBlocks.count(b)) {
                participatesInLoop = true;
                break;
            }
        }
    }

    if (cfg.hasCycle) {
        if (!cfg.hasNaturalLoop || !cfg.loopInfo.isCanonical) {
            if (pReason) *pReason = "Control flow contains unsupported cycles/loops (" + cfg.loopInfo.rejectionReason + "); multi-block promotion ineligible.";
            return false;
        }
        if (!participatesInLoop) {
            for (uint32_t b : blocksWithUses) {
                if (cfg.loopInfo.loopBlocks.count(b)) {
                    if (pReason) *pReason = "Allocation participates in loop body.";
                    return false;
                }
            }
        }
    }

    if (participatesInLoop) {
        // Section 9: Loop termination & safety invariants
        if (ir.hasLoop) {
            if (ir.loop.inductionStep == 0 || std::abs(ir.loop.inductionStep) != 1) {
                if (pReason) *pReason = "Unsupported induction step (" + std::to_string(ir.loop.inductionStep) + "); loop promotion ineligible.";
                return false;
            }
            if (ir.loop.breakCmpOpcode != LoopCmpOpcode::IGE && ir.loop.breakCmpOpcode != LoopCmpOpcode::UGE &&
                ir.loop.breakCmpOpcode != LoopCmpOpcode::ILT && ir.loop.breakCmpOpcode != LoopCmpOpcode::ULT &&
                ir.loop.breakCmpOpcode != LoopCmpOpcode::IEQ && ir.loop.breakCmpOpcode != LoopCmpOpcode::INE) {
                if (pReason) *pReason = "Unsupported loop comparison opcode; loop promotion ineligible.";
                return false;
            }
        }

        // Check if preheader stores to this allocation
        bool preheaderStores = false;
        uint32_t preheader = cfg.loopInfo.preheader;
        if (preheader < ir.basicBlocks.size()) {
            for (const auto& op : ir.basicBlocks[preheader].instructions) {
                if (op.isLocalMemOp && op.localSlot == alloc.slotIndex && op.isLocalStore) {
                    preheaderStores = true;
                    break;
                }
            }
        }

        // Count stores in loop blocks
        uint32_t loopStoreCount = 0;
        for (uint32_t b : cfg.loopInfo.loopBlocks) {
            if (b >= ir.basicBlocks.size()) continue;
            for (const auto& op : ir.basicBlocks[b].instructions) {
                if (op.isLocalMemOp && op.localSlot == alloc.slotIndex && op.isLocalStore) {
                    loopStoreCount++;
                }
            }
        }

        if (blocksWithUses.size() == 1) {
            // Loop-local variable confined to a single block
            // Verify that within this block, store appears before first load
            uint32_t targetBlock = *blocksWithUses.begin();
            bool seenStore = false;
            for (const auto& op : ir.basicBlocks[targetBlock].instructions) {
                if (op.isLocalMemOp && op.localSlot == alloc.slotIndex) {
                    if (op.isLocalStore) {
                        seenStore = true;
                        break;
                    } else {
                        break;
                    }
                }
            }
            if (!seenStore) {
                if (pReason) *pReason = "Load appears before reaching store (uninitialized loop storage).";
                return false;
            }
        } else if (loopStoreCount == 0) {
            // Category A: Loop-Invariant Local Scalar
            if (!preheaderStores) {
                if (pReason) *pReason = "Load appears before reaching store (uninitialized loop storage).";
                return false;
            }
        } else {
            // Category B: Loop-Carried Scalar Variable or Induction Variable
            if (!preheaderStores) {
                bool headerStoresBeforeLoad = false;
                for (const auto& op : ir.basicBlocks[cfg.loopInfo.header].instructions) {
                    if (op.isLocalMemOp && op.localSlot == alloc.slotIndex) {
                        if (op.isLocalStore) { headerStoresBeforeLoad = true; break; }
                        else { break; }
                    }
                }
                if (!headerStoresBeforeLoad) {
                    if (pReason) *pReason = "Load appears before reaching store (uninitialized loop storage).";
                    return false;
                }
            }

            // Check if backedge is critical and unsplittable
            uint32_t latch = cfg.loopInfo.latch;
            uint32_t header = cfg.loopInfo.header;
            if (cfg.succs.at(latch).size() > 1 && cfg.preds.at(header).size() > 1) {
                if (latch < ir.basicBlocks.size() && ir.basicBlocks[latch].terminator.kind != BlockTerminatorKind::ConditionalBranch) {
                    if (pReason) *pReason = "CFG contains critical edge (" + std::to_string(latch) + " -> " + std::to_string(header) +
                                            ") requiring edge-specific assignment; loop promotion ineligible.";
                    return false;
                }
            }
        }
    }

    // Rule 6: CFG Constraints for Multi-Block Scenarios (Acyclic)
    if (!participatesInLoop && blocksWithUses.size() > 1) {
        if (!cfg.isConsistent) {
            if (pReason) *pReason = "CFG is malformed or inconsistent: " + cfg.diagnostic;
            return false;
        }
        if (cfg.isIrreducible) {
            if (pReason) *pReason = "Irreducible control flow graph.";
            return false;
        }
        if (cfg.exceedsBlockLimit || cfg.reachableBlocks.size() > 16) {
            if (pReason) *pReason = "CFG exceeds bounded 16-block envelope.";
            return false;
        }
        for (uint32_t b : blocksWithUses) {
            if (!cfg.reachableBlocks.count(b)) {
                if (pReason) *pReason = "Allocation used in unreachable basic block " + std::to_string(b) + ".";
                return false;
            }
        }

        // Critical edge validation for merge blocks:
        std::set<uint32_t> mergeBlocks = CFGAnalysis::ComputeIDF(cfg, storeBlocks);
        for (uint32_t m : mergeBlocks) {
            auto itP = cfg.preds.find(m);
            if (itP != cfg.preds.end()) {
                for (uint32_t p : itP->second) {
                    auto itS = cfg.succs.find(p);
                    if (itS != cfg.succs.end() && itS->second.size() > 1) {
                        // Edge p -> m is critical!
                        // If p is a Switch or other non-splittable block, it cannot be safely split
                        if (p < ir.basicBlocks.size() && ir.basicBlocks[p].terminator.kind == BlockTerminatorKind::Switch) {
                            if (pReason) *pReason = "CFG contains critical edge (" + std::to_string(p) + " -> " + 
                                                    std::to_string(m) + ") requiring edge-specific assignment; multi-block promotion ineligible.";
                            return false;
                        }
                    }
                }
            }
        }
    }

    // Rule 7: Reaching Definition / Dominance (Acyclic Scenarios)
    if (!participatesInLoop) {
        if (blocksWithUses.size() == 1) {
            // Single block: verify every load is preceded by a store
            uint32_t targetBlock = *blocksWithUses.begin();
            bool seenStore = false;
            for (const auto& op : ir.basicBlocks[targetBlock].instructions) {
                if (op.isLocalMemOp && op.localSlot == alloc.slotIndex) {
                    if (op.isLocalStore) {
                        seenStore = true;
                    } else {
                        if (!seenStore) {
                            if (pReason) *pReason = "Load appears before reaching store (uninitialized storage).";
                            return false;
                        }
                    }
                }
            }
        } else {
            // Multi-block: verify that for every load, every reachable incoming path has a store
            for (uint32_t lBlock : loadBlocks) {
                // Check if lBlock stores before its first load
                bool storesBeforeFirstLoad = false;
                for (const auto& op : ir.basicBlocks[lBlock].instructions) {
                    if (op.isLocalMemOp && op.localSlot == alloc.slotIndex) {
                        if (op.isLocalStore) {
                            storesBeforeFirstLoad = true;
                            break;
                        } else {
                            break;
                        }
                    }
                }
                if (storesBeforeFirstLoad) continue;

                // Otherwise, entry must not be able to reach lBlock without encountering a store block
                std::set<uint32_t> visited;
                std::vector<uint32_t> q;
                if (!storeBlocks.count(cfg.entryBlock)) {
                    visited.insert(cfg.entryBlock);
                    q.push_back(cfg.entryBlock);
                }
                bool reachedUninitialized = false;
                size_t head = 0;
                while (head < q.size()) {
                    uint32_t u = q[head++];
                    if (u == lBlock) {
                        reachedUninitialized = true;
                        break;
                    }
                    auto itSucc = cfg.succs.find(u);
                    if (itSucc != cfg.succs.end()) {
                        for (uint32_t s : itSucc->second) {
                            if (storeBlocks.count(s)) continue; // Path stopped by store
                            if (visited.insert(s).second) {
                                q.push_back(s);
                            }
                        }
                    }
                }
                if (reachedUninitialized) {
                    if (pReason) *pReason = "Load appears before reaching store (uninitialized storage).";
                    return false;
                }
            }
        }
    }

    if (storeCount == 0) {
        if (pReason) *pReason = "Allocation has zero stores.";
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Promotion Pass Execution
// ----------------------------------------------------------------------------

bool DXILMem2Reg::Run(
    DXILIntermediateRepresentation& ir,
    const Mem2RegOptions& options,
    Mem2RegStats& outStats,
    DXILTranslationStatus& outStatus,
    std::string& outDiagnostic)
{
    outStats = Mem2RegStats();
    outStats.allocationsDiscovered = static_cast<uint32_t>(ir.localAllocs.size());

    if (!options.enabled) {
        outStats.allocationsUnpromoted = outStats.allocationsDiscovered;
        outStats.isVerified = true;
        outStats.verificationDiagnostic = "Mem2Reg pass disabled by options.";
        outStatus = DXILTranslationStatus::Success;
        outDiagnostic = "Mem2Reg pass disabled.";
        return true;
    }

    if (ir.localAllocs.empty()) {
        outStats.isVerified = true;
        outStats.verificationDiagnostic = "No local allocations to optimize.";
        outStatus = DXILTranslationStatus::Success;
        outDiagnostic = "No local allocations to optimize.";
        return true;
    }

    DXILIntermediateRepresentation irBackup = ir;

    // Step 1: Perform CFG analysis
    CFGAnalysis cfg;
    if (!CFGAnalysis::Analyze(ir, cfg)) {
        outStats.allocationsUnpromoted = outStats.allocationsDiscovered;
        outStats.isVerified = true;
        outStats.verificationDiagnostic = "CFG analysis failed: " + cfg.diagnostic;
        outStatus = DXILTranslationStatus::Success;
        outDiagnostic = "CFG analysis failed: " + cfg.diagnostic;
        return true;
    }

    // Step 2: Discover all eligible allocations
    std::set<uint32_t> eligibleSlots;
    for (size_t s = 0; s < ir.localAllocs.size(); ++s) {
        std::string reason;
        if (IsEligibleWithCFG(ir, cfg, static_cast<uint32_t>(s), &reason)) {
            eligibleSlots.insert(ir.localAllocs[s].slotIndex);
            outStats.allocationsEligible++;
        }
    }

    if (eligibleSlots.empty()) {
        outStats.allocationsUnpromoted = outStats.allocationsDiscovered;
        outStats.isVerified = true;
        outStats.verificationDiagnostic = "No eligible allocations for Mem2Reg promotion.";
        outStatus = DXILTranslationStatus::Success;
        outDiagnostic = "No eligible allocations for Mem2Reg promotion.";
        return true;
    }

    // Step 2.5: Bounded Critical Edge Splitting for Eligible Multi-Block & Loop Allocations
    std::map<uint32_t, uint32_t> splitBlockToOriginalPred;
    if (options.splitCriticalEdges) {
        if (cfg.hasNaturalLoop && cfg.loopInfo.isCanonical) {
            uint32_t latch = cfg.loopInfo.latch;
            uint32_t header = cfg.loopInfo.header;
            if (cfg.succs[latch].size() > 1 && cfg.preds[header].size() > 1) {
                if (latch < ir.basicBlocks.size() && ir.basicBlocks[latch].terminator.kind == BlockTerminatorKind::ConditionalBranch) {
                    uint32_t splitId = 0;
                    if (CFGAnalysis::SplitCriticalEdge(ir, cfg, latch, header, splitId)) {
                        outStats.criticalEdgesSplit++;
                        splitBlockToOriginalPred[splitId] = latch;
                    }
                }
            }
        }

        for (uint32_t slotIdx : eligibleSlots) {
            std::set<uint32_t> defBlocks;
            for (uint32_t b = 0; b < ir.numBasicBlocks; ++b) {
                if (b >= ir.basicBlocks.size()) break;
                for (const auto& op : ir.basicBlocks[b].instructions) {
                    if (op.isLocalMemOp && op.localSlot == slotIdx && op.isLocalStore) {
                        defBlocks.insert(b);
                    }
                }
            }
            std::set<uint32_t> mergeBlocks = CFGAnalysis::ComputeIDF(cfg, defBlocks);
            for (uint32_t m : mergeBlocks) {
                auto itP = cfg.preds.find(m);
                if (itP != cfg.preds.end()) {
                    std::vector<uint32_t> pList = itP->second;
                    for (uint32_t p : pList) {
                        auto itS = cfg.succs.find(p);
                        if (itS != cfg.succs.end() && itS->second.size() > 1) {
                            if (p < ir.basicBlocks.size() && ir.basicBlocks[p].terminator.kind == BlockTerminatorKind::ConditionalBranch) {
                                uint32_t splitId = 0;
                                if (CFGAnalysis::SplitCriticalEdge(ir, cfg, p, m, splitId)) {
                                    outStats.criticalEdgesSplit++;
                                    splitBlockToOriginalPred[splitId] = p;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Global replacements: loadSsa -> reaching definition
    std::map<uint32_t, IRValue> ssaReplacements;
    std::set<uint32_t> successfullyPromotedSlots;
    uint32_t nextPhiSsa = 5000; // Distinct SSA range for synthetic PHI nodes
    uint32_t phiTempBase = 1;

    // Step 3: Transactional Promotion per eligible allocation
    for (uint32_t slotIdx : eligibleSlots) {
        const LocalAllocDescriptor* pAlloc = nullptr;
        for (const auto& a : ir.localAllocs) {
            if (a.slotIndex == slotIdx) {
                pAlloc = &a;
                break;
            }
        }
        if (!pAlloc) continue;

        // Collect stores and loads for this slot
        std::set<uint32_t> defBlocks;
        std::set<uint32_t> useBlocks;
        uint32_t slotStoreCount = 0;
        uint32_t slotLoadCount = 0;

        for (uint32_t b = 0; b < ir.numBasicBlocks; ++b) {
            if (b >= ir.basicBlocks.size()) break;
            for (const auto& op : ir.basicBlocks[b].instructions) {
                if (op.isLocalMemOp && op.localSlot == slotIdx) {
                    useBlocks.insert(b);
                    if (op.isLocalStore) {
                        defBlocks.insert(b);
                        slotStoreCount++;
                    } else {
                        slotLoadCount++;
                    }
                }
            }
        }

        bool isLoopAlloc = (cfg.hasNaturalLoop && cfg.loopInfo.isCanonical);
        if (isLoopAlloc) {
            bool intersects = false;
            for (uint32_t b : useBlocks) {
                if (cfg.loopInfo.loopBlocks.count(b)) {
                    intersects = true;
                    break;
                }
            }
            isLoopAlloc = intersects;
            if (isLoopAlloc && useBlocks.size() == 1) {
                // If all uses are in a single block inside the loop,
                // check if the preheader stored to this slot.
                // If not, it is a loop-local variable (promoted via single-block straight-line path).
                bool preheaderStores = false;
                uint32_t preheader = cfg.loopInfo.preheader;
                if (preheader < ir.basicBlocks.size()) {
                    for (const auto& op : ir.basicBlocks[preheader].instructions) {
                        if (op.isLocalMemOp && op.localSlot == slotIdx && op.isLocalStore) {
                            preheaderStores = true;
                            break;
                        }
                    }
                }
                if (!preheaderStores) {
                    isLoopAlloc = false;
                }
            }
        }

        if (isLoopAlloc) {
            uint32_t loopStoreCount = 0;
            for (uint32_t b : cfg.loopInfo.loopBlocks) {
                if (b >= ir.basicBlocks.size()) continue;
                for (const auto& op : ir.basicBlocks[b].instructions) {
                    if (op.isLocalMemOp && op.localSlot == slotIdx && op.isLocalStore) {
                        loopStoreCount++;
                    }
                }
            }

            if (loopStoreCount == 0) {
                // === Category A: Loop-Invariant Local Scalar ===
                IRValue preheaderDef;
                uint32_t preheader = cfg.loopInfo.preheader;
                for (const auto& op : ir.basicBlocks[preheader].instructions) {
                    if (op.isLocalMemOp && op.localSlot == slotIdx && op.isLocalStore) {
                        if (op.hasCBuffer) {
                            preheaderDef = IRValue::MakeCBuffer(0, op.cbufferReg, op.cbufferChannel, 0);
                        } else if (op.isScalarIntOp) {
                            preheaderDef = IRValue::MakeLiteralInt(op.literalInt, 0);
                        } else if (op.src0[0].type == IRValueType::LiteralFloat) {
                            preheaderDef = op.src0[0];
                        } else {
                            preheaderDef = op.src0[0];
                            if (ssaReplacements.count(preheaderDef.ssaId)) {
                                preheaderDef = ssaReplacements[preheaderDef.ssaId];
                            }
                        }
                    }
                }

                std::map<uint32_t, IRValue> localReplacements;
                std::vector<PromotedLoadRecord> localLoadRecords;

                for (uint32_t b : useBlocks) {
                    if (b >= ir.basicBlocks.size()) continue;
                    for (size_t opIdx = 0; opIdx < ir.basicBlocks[b].instructions.size(); ++opIdx) {
                        auto& op = ir.basicBlocks[b].instructions[opIdx];
                        if (op.isLocalMemOp && op.localSlot == slotIdx && !op.isLocalStore) {
                            uint32_t loadSsa = op.destTempIndex;
                            localReplacements[loadSsa] = preheaderDef;

                            PromotedLoadRecord rec;
                            rec.loadSsa = loadSsa;
                            rec.slotIndex = slotIdx;
                            rec.subSlotIndex = op.localSubSlot;
                            rec.blockId = b;
                            rec.instPosition = static_cast<uint32_t>(opIdx);
                            rec.reachingDefId = preheaderDef.ssaId;
                            rec.reachingValue = preheaderDef;
                            rec.isPhiDef = false;
                            rec.isLoopInvariant = true;
                            rec.isLoopCarried = false;
                            localLoadRecords.push_back(rec);
                        }
                    }
                }

                for (const auto& pair : localReplacements) {
                    ssaReplacements[pair.first] = pair.second;
                    ir.ssaValues[pair.first] = pair.second;
                }
                outStats.promotedLoads.insert(outStats.promotedLoads.end(), localLoadRecords.begin(), localLoadRecords.end());
                outStats.loadsReplaced += static_cast<uint32_t>(localLoadRecords.size());
                outStats.storesEliminated += slotStoreCount;
                outStats.loopInvariantAllocationsPromoted++;
                outStats.allocationsPromoted++;
                successfullyPromotedSlots.insert(slotIdx);
            } else {
                // === Category B: Loop-Carried Scalar (Accumulator / Induction) ===
                IRValue initialValue;
                uint32_t preheader = cfg.loopInfo.preheader;
                for (const auto& op : ir.basicBlocks[preheader].instructions) {
                    if (op.isLocalMemOp && op.localSlot == slotIdx && op.isLocalStore) {
                        if (op.hasCBuffer) {
                            initialValue = IRValue::MakeCBuffer(0, op.cbufferReg, op.cbufferChannel, 0);
                        } else if (op.isScalarIntOp) {
                            initialValue = IRValue::MakeLiteralInt(op.literalInt, 0);
                        } else if (op.src0[0].type == IRValueType::LiteralFloat) {
                            initialValue = op.src0[0];
                        } else {
                            initialValue = op.src0[0];
                            if (ssaReplacements.count(initialValue.ssaId)) {
                                initialValue = ssaReplacements[initialValue.ssaId];
                            }
                        }
                    }
                }

                uint32_t phiDestTemp = phiTempBase++;
                uint32_t phiSsa = nextPhiSsa++;
                uint32_t header = cfg.loopInfo.header;
                uint32_t latch = cfg.loopInfo.latch;

                DXILPHINode phi;
                phi.destSsa = phiSsa;
                phi.typeId = pAlloc->allocatedTypeId;
                phi.channel = 0;
                phi.mergeBlock = header;
                phi.slotIndex = slotIdx;
                phi.isMem2RegPhi = true;

                PHIIncoming preheaderInc;
                preheaderInc.incomingSsa = initialValue.ssaId;
                preheaderInc.predecessorBlock = preheader;
                preheaderInc.incomingValue = initialValue;
                if (initialValue.type == IRValueType::LiteralFloat) {
                    preheaderInc.isConstant = true;
                    preheaderInc.constantInt = initialValue.literalInt;
                }
                phi.incoming.push_back(preheaderInc);

                std::map<uint32_t, IRValue> localReplacements;
                std::vector<PromotedLoadRecord> localLoadRecords;

                IRValue phiVal = IRValue::MakeTemp(phiDestTemp, 0, phiSsa);
                IRValue currentDef = phiVal;

                std::vector<uint32_t> loopOrder;
                loopOrder.push_back(header);
                for (uint32_t b : cfg.loopInfo.loopBlocks) {
                    if (b != header && b != latch) loopOrder.push_back(b);
                }
                if (latch != header) loopOrder.push_back(latch);

                for (uint32_t b : loopOrder) {
                    if (b >= ir.basicBlocks.size()) continue;
                    for (size_t opIdx = 0; opIdx < ir.basicBlocks[b].instructions.size(); ++opIdx) {
                        auto& op = ir.basicBlocks[b].instructions[opIdx];
                        if (op.isLocalMemOp && op.localSlot == slotIdx) {
                            if (op.isLocalStore) {
                                if (op.hasCBuffer) {
                                    currentDef = IRValue::MakeCBuffer(0, op.cbufferReg, op.cbufferChannel, 0);
                                } else if (op.isScalarIntOp) {
                                    currentDef = IRValue::MakeLiteralInt(op.literalInt, 0);
                                } else if (op.src0[0].type == IRValueType::LiteralFloat) {
                                    currentDef = op.src0[0];
                                } else {
                                    currentDef = op.src0[0];
                                    if (localReplacements.count(currentDef.ssaId)) {
                                        currentDef = localReplacements[currentDef.ssaId];
                                    } else if (ssaReplacements.count(currentDef.ssaId)) {
                                        currentDef = ssaReplacements[currentDef.ssaId];
                                    }
                                }
                            } else {
                                uint32_t loadSsa = op.destTempIndex;
                                localReplacements[loadSsa] = currentDef;

                                PromotedLoadRecord rec;
                                rec.loadSsa = loadSsa;
                                rec.slotIndex = slotIdx;
                                rec.subSlotIndex = op.localSubSlot;
                                rec.blockId = b;
                                rec.instPosition = static_cast<uint32_t>(opIdx);
                                rec.reachingDefId = currentDef.ssaId;
                                rec.reachingValue = currentDef;
                                rec.isPhiDef = (currentDef.ssaId == phiSsa);
                                rec.isLoopInvariant = false;
                                rec.isLoopCarried = true;
                                localLoadRecords.push_back(rec);
                            }
                        }
                    }
                }

                IRValue updatedValue = currentDef;
                uint32_t actualLatchPred = latch;
                bool isBackEdgeSplit = false;
                uint32_t splitLandingBlock = 0;
                uint32_t originalLatch = latch;
                auto itSplit = splitBlockToOriginalPred.find(latch);
                if (itSplit != splitBlockToOriginalPred.end()) {
                    isBackEdgeSplit = true;
                    splitLandingBlock = itSplit->first;
                    originalLatch = itSplit->second;
                    actualLatchPred = splitLandingBlock;
                }

                PHIIncoming latchInc;
                latchInc.incomingSsa = updatedValue.ssaId;
                latchInc.predecessorBlock = actualLatchPred;
                latchInc.incomingValue = updatedValue;
                if (updatedValue.type == IRValueType::LiteralFloat) {
                    latchInc.isConstant = true;
                    latchInc.constantInt = updatedValue.literalInt;
                }
                phi.incoming.push_back(latchInc);

                // Trace reaching definitions through the loop blocks to determine
                // the exact reaching definition leaving the exiting block.
                IRValue exitReachingDef = phiVal;
                uint32_t exitingBlock = cfg.loopInfo.exitingBlock;

                std::map<uint32_t, IRValue> blockReachingDef;
                blockReachingDef[header] = phiVal;

                for (uint32_t b : loopOrder) {
                    IRValue bDef = phiVal;
                    if (cfg.preds.count(b)) {
                        for (uint32_t p : cfg.preds.at(b)) {
                            if (b != header && blockReachingDef.count(p)) {
                                bDef = blockReachingDef[p];
                                break;
                            }
                        }
                    }
                    if (b < ir.basicBlocks.size()) {
                        for (const auto& op : ir.basicBlocks[b].instructions) {
                            if (op.isLocalMemOp && op.localSlot == slotIdx && op.isLocalStore) {
                                if (op.hasCBuffer) {
                                    bDef = IRValue::MakeCBuffer(0, op.cbufferReg, op.cbufferChannel, 0);
                                } else if (op.isScalarIntOp) {
                                    bDef = IRValue::MakeLiteralInt(op.literalInt, 0);
                                } else if (op.src0[0].type == IRValueType::LiteralFloat) {
                                    bDef = op.src0[0];
                                } else {
                                    bDef = op.src0[0];
                                    if (localReplacements.count(bDef.ssaId)) {
                                        bDef = localReplacements[bDef.ssaId];
                                    } else if (ssaReplacements.count(bDef.ssaId)) {
                                        bDef = ssaReplacements[bDef.ssaId];
                                    }
                                }
                            }
                        }
                    }
                    blockReachingDef[b] = bDef;
                }

                if (blockReachingDef.count(exitingBlock)) {
                    exitReachingDef = blockReachingDef[exitingBlock];
                }

                for (uint32_t b : useBlocks) {
                    if (cfg.loopInfo.loopBlocks.count(b) || b == preheader) continue;
                    if (b >= ir.basicBlocks.size()) continue;
                    for (size_t opIdx = 0; opIdx < ir.basicBlocks[b].instructions.size(); ++opIdx) {
                        auto& op = ir.basicBlocks[b].instructions[opIdx];
                        if (op.isLocalMemOp && op.localSlot == slotIdx && !op.isLocalStore) {
                            uint32_t loadSsa = op.destTempIndex;
                            localReplacements[loadSsa] = exitReachingDef;

                            PromotedLoadRecord rec;
                            rec.loadSsa = loadSsa;
                            rec.slotIndex = slotIdx;
                            rec.subSlotIndex = op.localSubSlot;
                            rec.blockId = b;
                            rec.instPosition = static_cast<uint32_t>(opIdx);
                            rec.reachingDefId = exitReachingDef.ssaId;
                            rec.reachingValue = exitReachingDef;
                            rec.isPhiDef = (exitReachingDef.ssaId == phiSsa);
                            rec.isLoopInvariant = false;
                            rec.isLoopCarried = true;
                            localLoadRecords.push_back(rec);
                        }
                    }
                }

                // Predecessor moves
                bool preheaderWrites = false;
                for (const auto& op : ir.basicBlocks[preheader].instructions) {
                    if (op.destTempIndex == phiDestTemp) { preheaderWrites = true; break; }
                }
                if (!preheaderWrites) {
                    IRVectorOp movOp;
                    movOp.opcode = IROpcode::Move;
                    movOp.destTempIndex = phiDestTemp;
                    if (initialValue.type == IRValueType::CBufferComponent) {
                        movOp.hasCBuffer = true;
                        movOp.cbufferReg = initialValue.regIndex;
                        movOp.cbufferChannel = initialValue.channel;
                    } else {
                        movOp.src0[0] = initialValue;
                    }
                    ir.basicBlocks[preheader].instructions.push_back(movOp);
                }

                PHIEdgeAssignmentRecord preRec;
                preRec.phiSsa = phiSsa;
                preRec.slotIndex = slotIdx;
                preRec.mergeBlock = header;
                preRec.predecessorBlock = preheader;
                preRec.destTempReg = phiDestTemp;
                preRec.incomingValue = initialValue;
                preRec.incomingSsa = initialValue.ssaId;
                preRec.isAssignmentEmitted = true;
                preRec.isSplitEdge = false;
                preRec.branchEdgeDescription = "Preheader B" + std::to_string(preheader) + " -> LoopHeader H" + std::to_string(header);
                preRec.assignmentDisasm = preRec.branchEdgeDescription + ": mov r" + std::to_string(phiDestTemp) + ", init";
                outStats.phiEdgeAssignments.push_back(preRec);

                uint32_t assignBlock = isBackEdgeSplit ? splitLandingBlock : latch;
                bool latchWrites = false;
                for (const auto& op : ir.basicBlocks[assignBlock].instructions) {
                    if (op.destTempIndex == phiDestTemp) { latchWrites = true; break; }
                }
                if (!latchWrites) {
                    IRVectorOp movOp;
                    movOp.opcode = IROpcode::Move;
                    movOp.destTempIndex = phiDestTemp;
                    if (updatedValue.type == IRValueType::CBufferComponent) {
                        movOp.hasCBuffer = true;
                        movOp.cbufferReg = updatedValue.regIndex;
                        movOp.cbufferChannel = updatedValue.channel;
                    } else {
                        movOp.src0[0] = updatedValue;
                    }
                    ir.basicBlocks[assignBlock].instructions.push_back(movOp);
                }

                PHIEdgeAssignmentRecord latchRec;
                latchRec.phiSsa = phiSsa;
                latchRec.slotIndex = slotIdx;
                latchRec.mergeBlock = header;
                latchRec.predecessorBlock = isBackEdgeSplit ? originalLatch : latch;
                latchRec.splitBlockId = isBackEdgeSplit ? splitLandingBlock : 0;
                latchRec.isSplitEdge = isBackEdgeSplit;
                latchRec.destTempReg = phiDestTemp;
                latchRec.incomingValue = updatedValue;
                latchRec.incomingSsa = updatedValue.ssaId;
                latchRec.isAssignmentEmitted = true;
                latchRec.branchEdgeDescription = isBackEdgeSplit ?
                    ("SplitBackEdge Latch B" + std::to_string(originalLatch) + " -> SplitBlock B" + std::to_string(splitLandingBlock) + " -> LoopHeader H" + std::to_string(header)) :
                    ("DirectBackEdge Latch B" + std::to_string(latch) + " -> LoopHeader H" + std::to_string(header));
                latchRec.assignmentDisasm = latchRec.branchEdgeDescription + ": mov r" + std::to_string(phiDestTemp) + ", update";
                outStats.phiEdgeAssignments.push_back(latchRec);

                InsertedPhiRecord phiRec;
                phiRec.phiSsa = phiSsa;
                phiRec.slotIndex = slotIdx;
                phiRec.mergeBlock = header;
                phiRec.typeKind = pAlloc->elemKind;
                for (const auto& in : phi.incoming) {
                    phiRec.incomingValues.push_back({ in.predecessorBlock, in.incomingValue });
                }
                outStats.insertedPhis.push_back(phiRec);
                ir.basicBlocks[header].phiNodes.push_back(phi);

                LoopPhiRecord lPhiRec;
                lPhiRec.slotIndex = slotIdx;
                lPhiRec.phiSsa = phiSsa;
                lPhiRec.phiTempReg = phiDestTemp;
                lPhiRec.preheaderBlock = preheader;
                lPhiRec.headerBlock = header;
                lPhiRec.latchBlock = isBackEdgeSplit ? originalLatch : latch;
                lPhiRec.backEdgeDestBlock = header;
                lPhiRec.loopExitBlock = cfg.loopInfo.exitBlock;
                lPhiRec.initialValue = initialValue;
                lPhiRec.latchIncomingValue = updatedValue;
                lPhiRec.exitReachingDef = exitReachingDef;
                lPhiRec.splitLandingBlock = splitLandingBlock;
                lPhiRec.isBackEdgeSplit = isBackEdgeSplit;
                outStats.loopPhis.push_back(lPhiRec);

                for (const auto& pair : localReplacements) {
                    ssaReplacements[pair.first] = pair.second;
                    ir.ssaValues[pair.first] = pair.second;
                }
                outStats.promotedLoads.insert(outStats.promotedLoads.end(), localLoadRecords.begin(), localLoadRecords.end());
                outStats.loadsReplaced += static_cast<uint32_t>(localLoadRecords.size());
                outStats.storesEliminated += slotStoreCount;
                outStats.loopCarriedAllocationsPromoted++;
                outStats.loopHeaderPhisInserted++;
                outStats.backEdgeAssignmentsEmitted++;
                outStats.phisInserted++;
                outStats.allocationsPromoted++;
                successfullyPromotedSlots.insert(slotIdx);
            }
        } else if (useBlocks.size() == 1) {
            // === Single-Block Straight-Line Promotion ===
            uint32_t b = *useBlocks.begin();
            std::map<uint32_t, IRValue> localReplacements;
            std::vector<PromotedLoadRecord> localLoadRecords;
            IRValue currentReachingDef;
            bool hasDef = false;

            for (size_t opIdx = 0; opIdx < ir.basicBlocks[b].instructions.size(); ++opIdx) {
                auto& op = ir.basicBlocks[b].instructions[opIdx];
                if (op.isLocalMemOp && op.localSlot == slotIdx) {
                    if (op.isLocalStore) {
                        if (op.hasCBuffer) {
                            currentReachingDef = IRValue::MakeCBuffer(0, op.cbufferReg, op.cbufferChannel, 0);
                        } else if (op.isScalarIntOp) {
                            currentReachingDef = IRValue::MakeLiteralInt(op.literalInt, 0);
                        } else if (op.src0[0].type == IRValueType::LiteralFloat) {
                            currentReachingDef = op.src0[0];
                        } else {
                            currentReachingDef = op.src0[0];
                            if (localReplacements.count(currentReachingDef.ssaId)) {
                                currentReachingDef = localReplacements[currentReachingDef.ssaId];
                            } else if (ssaReplacements.count(currentReachingDef.ssaId)) {
                                currentReachingDef = ssaReplacements[currentReachingDef.ssaId];
                            }
                        }
                        hasDef = true;
                    } else {
                        if (!hasDef) break; // Should not happen due to IsEligible
                        uint32_t loadSsa = op.destTempIndex;
                        localReplacements[loadSsa] = currentReachingDef;

                        PromotedLoadRecord rec;
                        rec.loadSsa = loadSsa;
                        rec.slotIndex = slotIdx;
                        rec.subSlotIndex = op.localSubSlot;
                        rec.blockId = b;
                        rec.instPosition = static_cast<uint32_t>(opIdx);
                        rec.reachingDefId = currentReachingDef.ssaId;
                        rec.reachingValue = currentReachingDef;
                        rec.isPhiDef = false;
                        localLoadRecords.push_back(rec);
                    }
                }
            }

            // Commit single-block promotion
            for (const auto& pair : localReplacements) {
                ssaReplacements[pair.first] = pair.second;
                ir.ssaValues[pair.first] = pair.second;
            }
            outStats.promotedLoads.insert(outStats.promotedLoads.end(), localLoadRecords.begin(), localLoadRecords.end());
            outStats.loadsReplaced += static_cast<uint32_t>(localLoadRecords.size());
            outStats.storesEliminated += slotStoreCount;
            successfullyPromotedSlots.insert(slotIdx);
            outStats.allocationsPromoted++;
        } else {
            // === Multi-Block Branch-Aware Promotion (IDF PHI Placement & SSA Renaming) ===
            std::set<uint32_t> mergeBlocks = CFGAnalysis::ComputeIDF(cfg, defBlocks);
            uint32_t phiDestTemp = phiTempBase++;

            std::map<uint32_t, DXILPHINode> blockPhiNodes; // mergeBlock -> inserted PHI
            for (uint32_t m : mergeBlocks) {
                DXILPHINode phi;
                phi.destSsa = nextPhiSsa++;
                phi.typeId = pAlloc->allocatedTypeId;
                phi.channel = 0;
                phi.mergeBlock = m;
                phi.slotIndex = slotIdx;
                phi.isMem2RegPhi = true;
                blockPhiNodes[m] = phi;
            }

            // Dominator-Tree Renaming Traversal
            std::vector<IRValue> defStack;
            std::map<uint32_t, IRValue> localReplacements;
            std::vector<PromotedLoadRecord> localLoadRecords;
            bool renamingFailed = false;

            std::function<void(uint32_t)> renameBlock = [&](uint32_t b) {
                if (renamingFailed) return;
                size_t stackSizeOnEntry = defStack.size();

                // If block has inserted PHI, push PHI definition
                if (blockPhiNodes.count(b)) {
                    IRValue phiVal = IRValue::MakeTemp(phiDestTemp, 0, blockPhiNodes[b].destSsa);
                    defStack.push_back(phiVal);
                }

                // Process block instructions
                for (size_t opIdx = 0; opIdx < ir.basicBlocks[b].instructions.size(); ++opIdx) {
                    auto& op = ir.basicBlocks[b].instructions[opIdx];
                    if (op.isLocalMemOp && op.localSlot == slotIdx) {
                        if (op.isLocalStore) {
                            IRValue storedVal;
                            if (op.hasCBuffer) {
                                storedVal = IRValue::MakeCBuffer(0, op.cbufferReg, op.cbufferChannel, 0);
                            } else if (op.isScalarIntOp) {
                                storedVal = IRValue::MakeLiteralInt(op.literalInt, 0);
                            } else if (op.src0[0].type == IRValueType::LiteralFloat) {
                                storedVal = op.src0[0];
                            } else {
                                storedVal = op.src0[0];
                                if (localReplacements.count(storedVal.ssaId)) {
                                    storedVal = localReplacements[storedVal.ssaId];
                                } else if (ssaReplacements.count(storedVal.ssaId)) {
                                    storedVal = ssaReplacements[storedVal.ssaId];
                                }
                            }
                            defStack.push_back(storedVal);
                        } else {
                            // Load
                            if (defStack.empty()) {
                                renamingFailed = true;
                                return;
                            }
                            IRValue reachVal = defStack.back();
                            uint32_t loadSsa = op.destTempIndex;
                            localReplacements[loadSsa] = reachVal;

                            PromotedLoadRecord rec;
                            rec.loadSsa = loadSsa;
                            rec.slotIndex = slotIdx;
                            rec.subSlotIndex = op.localSubSlot;
                            rec.blockId = b;
                            rec.instPosition = static_cast<uint32_t>(opIdx);
                            rec.reachingDefId = reachVal.ssaId;
                            rec.reachingValue = reachVal;
                            rec.isPhiDef = (reachVal.type == IRValueType::IntermediateTemp && blockPhiNodes.count(b));
                            localLoadRecords.push_back(rec);
                        }
                    }
                }

                // Fill PHI incoming values for CFG successors
                auto itSucc = cfg.succs.find(b);
                if (itSucc != cfg.succs.end()) {
                    for (uint32_t succ : itSucc->second) {
                        if (blockPhiNodes.count(succ)) {
                            if (defStack.empty()) {
                                renamingFailed = true;
                                return;
                            }
                            IRValue inVal = defStack.back();
                            PHIIncoming phiInc;
                            phiInc.incomingSsa = inVal.ssaId;
                            phiInc.predecessorBlock = b;
                            phiInc.incomingValue = inVal;
                            if (inVal.type == IRValueType::LiteralFloat) {
                                phiInc.isConstant = true;
                                phiInc.constantInt = inVal.literalInt;
                            }
                            blockPhiNodes[succ].incoming.push_back(phiInc);
                        }
                    }
                }

                // Recurse into dominator tree children
                auto itChildren = cfg.domTreeChildren.find(b);
                if (itChildren != cfg.domTreeChildren.end()) {
                    for (uint32_t child : itChildren->second) {
                        renameBlock(child);
                    }
                }

                // Pop definitions pushed in this block
                while (defStack.size() > stackSizeOnEntry) {
                    defStack.pop_back();
                }
            };

            renameBlock(cfg.entryBlock);

            if (renamingFailed) {
                // Transactional rollback: leave allocation unpromoted on Phase 12.8 fallback path
                continue;
            }

            // Redundant PHI simplification & Emitting predecessor moves
            for (auto& phiPair : blockPhiNodes) {
                uint32_t mBlock = phiPair.first;
                auto& phi = phiPair.second;

                // Check for redundant PHI (all incoming identical)
                bool allIdentical = true;
                if (!phi.incoming.empty()) {
                    const auto& firstVal = phi.incoming[0].incomingValue;
                    for (size_t k = 1; k < phi.incoming.size(); ++k) {
                        const auto& otherVal = phi.incoming[k].incomingValue;
                        if (firstVal.type != otherVal.type ||
                            firstVal.regIndex != otherVal.regIndex ||
                            firstVal.channel != otherVal.channel ||
                            firstVal.ssaId != otherVal.ssaId ||
                            firstVal.literalValue != otherVal.literalValue) {
                            allIdentical = false;
                            break;
                        }
                    }
                }

                if (allIdentical && !phi.incoming.empty()) {
                    // Simplify: rewrite all uses of phi.destSsa directly to firstVal
                    IRValue simplifiedVal = phi.incoming[0].incomingValue;
                    for (auto& lPair : localReplacements) {
                        if (lPair.second.ssaId == phi.destSsa) {
                            lPair.second = simplifiedVal;
                        }
                    }
                    for (auto& rec : localLoadRecords) {
                        if (rec.reachingValue.ssaId == phi.destSsa) {
                            rec.reachingValue = simplifiedVal;
                            rec.reachingDefId = simplifiedVal.ssaId;
                            rec.isPhiDef = false;
                        }
                    }
                    outStats.redundantPhisRemoved++;
                } else {
                    // Non-redundant PHI: emit moves into phiDestTemp in predecessor blocks
                    for (const auto& in : phi.incoming) {
                        uint32_t pBlock = in.predecessorBlock;
                        // Check if pBlock already writes to phiDestTemp
                        bool writesDest = false;
                        for (const auto& op : ir.basicBlocks[pBlock].instructions) {
                            if (op.destTempIndex == phiDestTemp) {
                                writesDest = true;
                                break;
                            }
                        }
                        if (!writesDest) {
                            IRVectorOp movOp;
                            movOp.opcode = IROpcode::Move;
                            movOp.destTempIndex = phiDestTemp;
                            if (in.incomingValue.type == IRValueType::CBufferComponent) {
                                movOp.hasCBuffer = true;
                                movOp.cbufferReg = in.incomingValue.regIndex;
                                movOp.cbufferChannel = in.incomingValue.channel;
                            } else if (in.incomingValue.type == IRValueType::LiteralFloat) {
                                movOp.src0[0] = in.incomingValue;
                            } else {
                                movOp.src0[0] = in.incomingValue;
                            }
                            ir.basicBlocks[pBlock].instructions.push_back(movOp);
                        }

                        // Record Edge Assignment Instrumentation
                        PHIEdgeAssignmentRecord edgeRec;
                        edgeRec.phiSsa = phi.destSsa;
                        edgeRec.slotIndex = slotIdx;
                        edgeRec.mergeBlock = mBlock;
                        edgeRec.destTempReg = phiDestTemp;
                        edgeRec.incomingValue = in.incomingValue;
                        edgeRec.isAssignmentEmitted = true;
                        edgeRec.incomingSsa = in.incomingValue.ssaId;

                        auto itSplit = splitBlockToOriginalPred.find(pBlock);
                        if (itSplit != splitBlockToOriginalPred.end()) {
                            edgeRec.isSplitEdge = true;
                            edgeRec.splitBlockId = pBlock;
                            edgeRec.predecessorBlock = itSplit->second;
                            edgeRec.branchEdgeDescription = "SplitEdge B" + std::to_string(itSplit->second) + " -> SplitBlock B" + std::to_string(pBlock) + " -> MergeBlock M" + std::to_string(mBlock);
                        } else {
                            edgeRec.isSplitEdge = false;
                            edgeRec.splitBlockId = 0;
                            edgeRec.predecessorBlock = pBlock;
                            edgeRec.branchEdgeDescription = "DirectEdge B" + std::to_string(pBlock) + " -> MergeBlock M" + std::to_string(mBlock);
                        }

                        edgeRec.assignmentDisasm = edgeRec.branchEdgeDescription +
                                                   ": mov r" + std::to_string(phiDestTemp) + ", " +
                                                   (in.incomingValue.type == IRValueType::CBufferComponent ? 
                                                    ("cb0[" + std::to_string(in.incomingValue.regIndex) + "]") :
                                                    (in.incomingValue.type == IRValueType::LiteralFloat ?
                                                     std::to_string(in.incomingValue.literalValue) :
                                                     ("r" + std::to_string(in.incomingValue.regIndex))));
                        outStats.phiEdgeAssignments.push_back(edgeRec);
                    }

                    // Record Inserted PHI
                    InsertedPhiRecord phiRec;
                    phiRec.phiSsa = phi.destSsa;
                    phiRec.slotIndex = slotIdx;
                    phiRec.mergeBlock = mBlock;
                    phiRec.typeKind = pAlloc->elemKind;
                    for (const auto& in : phi.incoming) {
                        phiRec.incomingValues.push_back({ in.predecessorBlock, in.incomingValue });
                    }
                    outStats.insertedPhis.push_back(phiRec);
                    outStats.phisInserted++;

                    // Populate PHI into basic block
                    ir.basicBlocks[mBlock].phiNodes.push_back(phi);
                }
            }

            // Commit multi-block promotion
            for (const auto& pair : localReplacements) {
                ssaReplacements[pair.first] = pair.second;
                ir.ssaValues[pair.first] = pair.second;
            }
            outStats.promotedLoads.insert(outStats.promotedLoads.end(), localLoadRecords.begin(), localLoadRecords.end());
            outStats.loadsReplaced += static_cast<uint32_t>(localLoadRecords.size());
            outStats.storesEliminated += slotStoreCount;
            successfullyPromotedSlots.insert(slotIdx);
            outStats.allocationsPromoted++;
        }
    }

    outStats.allocationsUnpromoted = outStats.allocationsDiscovered - outStats.allocationsPromoted;

    // Step 4: Eliminate promoted memory instructions from basic blocks
    for (uint32_t b = 0; b < ir.numBasicBlocks; ++b) {
        if (b >= ir.basicBlocks.size()) break;
        std::vector<IRVectorOp> remainingInsts;
        for (auto& op : ir.basicBlocks[b].instructions) {
            if (op.isLocalMemOp && successfullyPromotedSlots.count(op.localSlot)) {
                // Eliminate promoted store or load
                continue;
            }
            // Rewrite downstream operands consuming replaced loads
            for (int c = 0; c < 4; ++c) {
                auto it0 = ssaReplacements.find(op.src0[c].ssaId);
                if (it0 != ssaReplacements.end()) {
                    op.src0[c] = it0->second;
                    if (op.src0[c].type == IRValueType::CBufferComponent) {
                        op.hasCBuffer = true;
                        op.cbufferReg = op.src0[c].regIndex;
                        op.cbufferChannel = op.src0[c].channel;
                    }
                }
                auto it1 = ssaReplacements.find(op.src1[c].ssaId);
                if (it1 != ssaReplacements.end()) {
                    op.src1[c] = it1->second;
                    if (op.src1[c].type == IRValueType::CBufferComponent) {
                        op.hasCBuffer = true;
                        op.cbufferReg = op.src1[c].regIndex;
                        op.cbufferChannel = op.src1[c].channel;
                    }
                }
            }
            remainingInsts.push_back(op);
        }
        ir.basicBlocks[b].instructions = std::move(remainingInsts);
    }

    // Step 5: Rewrite downstream uses across all global IR structures
    for (auto& vOp : ir.vectorOps) {
        for (int c = 0; c < 4; ++c) {
            auto it0 = ssaReplacements.find(vOp.src0[c].ssaId);
            if (it0 != ssaReplacements.end()) {
                vOp.src0[c] = it0->second;
                if (vOp.src0[c].type == IRValueType::CBufferComponent) {
                    vOp.hasCBuffer = true;
                    vOp.cbufferReg = vOp.src0[c].regIndex;
                    vOp.cbufferChannel = vOp.src0[c].channel;
                }
            }
            auto it1 = ssaReplacements.find(vOp.src1[c].ssaId);
            if (it1 != ssaReplacements.end()) {
                vOp.src1[c] = it1->second;
                if (vOp.src1[c].type == IRValueType::CBufferComponent) {
                    vOp.hasCBuffer = true;
                    vOp.cbufferReg = vOp.src1[c].regIndex;
                    vOp.cbufferChannel = vOp.src1[c].channel;
                }
            }
        }
    }

    for (auto& pair : ir.comparisons) {
        auto& cmp = pair.second;
        auto it0 = ssaReplacements.find(cmp.src0.ssaId);
        if (it0 != ssaReplacements.end()) cmp.src0 = it0->second;
        auto it1 = ssaReplacements.find(cmp.src1.ssaId);
        if (it1 != ssaReplacements.end()) cmp.src1 = it1->second;
    }

    auto itSwitch = ssaReplacements.find(ir.switchDesc.selectorSsa);
    if (itSwitch != ssaReplacements.end()) {
        const auto& repl = itSwitch->second;
        if (repl.type == IRValueType::CBufferComponent) {
            ir.switchDesc.selectorIsCBuffer = true;
            ir.switchDesc.selectorCbRow = repl.regIndex;
            ir.switchDesc.selectorCbChannel = repl.channel;
        }
    }

    // Step 6: Remove successfully promoted allocations from ir.localAllocs
    std::vector<LocalAllocDescriptor> remainingAllocs;
    for (const auto& alloc : ir.localAllocs) {
        if (!successfullyPromotedSlots.count(alloc.slotIndex)) {
            remainingAllocs.push_back(alloc);
        }
    }
    ir.localAllocs = std::move(remainingAllocs);

    // Step 7: Recalculate temporary register allocation
    uint32_t maxTemp = 0;
    for (uint32_t b = 0; b < ir.numBasicBlocks; ++b) {
        if (b >= ir.basicBlocks.size()) break;
        for (const auto& op : ir.basicBlocks[b].instructions) {
            if (op.destTempIndex > maxTemp && !op.destIsOutput) {
                maxTemp = op.destTempIndex;
            }
        }
    }
    for (const auto& op : ir.vectorOps) {
        if (op.destTempIndex > maxTemp && !op.destIsOutput) {
            maxTemp = op.destTempIndex;
        }
    }

    uint32_t baseTemp = (maxTemp > 0) ? (maxTemp + 1) : 0;
    for (size_t s = 0; s < ir.localAllocs.size(); ++s) {
        ir.localAllocs[s].tempRegIndex = baseTemp + static_cast<uint32_t>(s);
    }
    ir.numTempsNeeded = baseTemp + static_cast<uint32_t>(ir.localAllocs.size());

    // Step 8: Strengthened SSA Verification
    if (options.verifySSA) {
        // Invariant 1: No remaining instruction refers to a promoted local memory slot
        for (uint32_t b = 0; b < ir.numBasicBlocks; ++b) {
            if (b >= ir.basicBlocks.size()) break;
            for (const auto& op : ir.basicBlocks[b].instructions) {
                if (op.isLocalMemOp && successfullyPromotedSlots.count(op.localSlot)) {
                    outStats.isVerified = false;
                    outStatus = DXILTranslationStatus::TranslationFailed;
                    outDiagnostic = "SSA Verification failed: Stale local memory instruction remains in Block " + 
                                    std::to_string(b) + " for promoted slot " + std::to_string(op.localSlot) + ".";
                    return false;
                }
            }
        }

        // Invariant 2: Every promoted allocation had its reaching definition established
        if (outStats.loadsReplaced > 0 && outStats.storesEliminated == 0) {
            outStats.isVerified = false;
            outStatus = DXILTranslationStatus::TranslationFailed;
            outDiagnostic = "SSA Verification failed: Loads replaced without reaching stores.";
            return false;
        }

        // Invariant 3: PHI predecessor consistency and incoming value validation
        for (const auto& phiRec : outStats.insertedPhis) {
            if (phiRec.mergeBlock >= ir.basicBlocks.size()) {
                outStats.isVerified = false;
                outStatus = DXILTranslationStatus::TranslationFailed;
                outDiagnostic = "SSA Verification failed: PHI belongs to out-of-range merge block " + std::to_string(phiRec.mergeBlock);
                return false;
            }
            const auto& requiredPreds = cfg.preds.at(phiRec.mergeBlock);
            if (phiRec.incomingValues.size() != requiredPreds.size()) {
                outStats.isVerified = false;
                outStatus = DXILTranslationStatus::TranslationFailed;
                outDiagnostic = "SSA Verification failed: PHI incoming count (" + std::to_string(phiRec.incomingValues.size()) +
                                ") does not match predecessor count (" + std::to_string(requiredPreds.size()) + ").";
                return false;
            }
            std::set<uint32_t> seenPreds;
            for (const auto& inPair : phiRec.incomingValues) {
                uint32_t p = inPair.first;
                if (std::find(requiredPreds.begin(), requiredPreds.end(), p) == requiredPreds.end()) {
                    outStats.isVerified = false;
                    outStatus = DXILTranslationStatus::TranslationFailed;
                    outDiagnostic = "SSA Verification failed: PHI incoming value cites invalid predecessor " + std::to_string(p);
                    ir = irBackup;
                    return false;
                }
                if (!seenPreds.insert(p).second) {
                    outStats.isVerified = false;
                    outStatus = DXILTranslationStatus::TranslationFailed;
                    outDiagnostic = "SSA Verification failed: Duplicate predecessor " + std::to_string(p) + " in PHI.";
                    ir = irBackup;
                    return false;
                }
            }
        }

        // Invariant 4: PHI Edge Assignment & DXBC emission verification
        for (const auto& edgeRec : outStats.phiEdgeAssignments) {
            uint32_t checkBlock = edgeRec.isSplitEdge ? edgeRec.splitBlockId : edgeRec.predecessorBlock;
            if (checkBlock >= ir.basicBlocks.size()) {
                outStats.isVerified = false;
                outStatus = DXILTranslationStatus::TranslationFailed;
                outDiagnostic = "SSA Verification failed: Assignment block " + std::to_string(checkBlock) + " out of bounds.";
                ir = irBackup;
                return false;
            }
            bool foundAssign = false;
            for (const auto& op : ir.basicBlocks[checkBlock].instructions) {
                if (op.destTempIndex == edgeRec.destTempReg) {
                    foundAssign = true;
                    break;
                }
            }
            if (!foundAssign) {
                outStats.isVerified = false;
                outStatus = DXILTranslationStatus::TranslationFailed;
                outDiagnostic = "SSA Verification failed: Assignment block " + std::to_string(checkBlock) +
                                " does not assign to PHI temp r" + std::to_string(edgeRec.destTempReg);
                ir = irBackup;
                return false;
            }
        }

        outStats.isVerified = true;
        outStats.verificationDiagnostic = "SSA verification passed: " + 
                                          std::to_string(outStats.allocationsPromoted) + " allocations promoted, " +
                                          std::to_string(outStats.phisInserted) + " PHIs inserted, " +
                                          std::to_string(outStats.loadsReplaced) + " loads replaced, " +
                                          std::to_string(outStats.storesEliminated) + " stores eliminated.";
    }

    outStatus = DXILTranslationStatus::Success;
    outDiagnostic = "Mem2Reg pass successfully completed.";
    return true;
}

} // namespace kepler
