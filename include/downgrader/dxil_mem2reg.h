#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "downgrader/dxil_bitcode_decoder.h"
#include "downgrader/dxil_translation.h"

namespace kepler {

/**
 * @brief Records comprehensive verification details for a promoted load.
 */
struct PromotedLoadRecord {
    uint32_t loadSsa = 0;
    uint32_t slotIndex = 0;
    uint32_t subSlotIndex = 0;
    uint32_t blockId = 0;
    uint32_t instPosition = 0;
    uint32_t reachingDefId = 0;
    IRValue reachingValue;
    bool isPhiDef = false;
    bool isLoopInvariant = false;
    bool isLoopCarried = false;
    std::vector<std::pair<uint32_t, IRValue>> phiPredecessors; // (predBlock, incomingVal)
    std::vector<std::string> rewrittenUses; // Descriptions of rewritten consumers
};

/**
 * @brief Records comprehensive verification details for an inserted PHI node.
 */
struct InsertedPhiRecord {
    uint32_t phiSsa = 0;
    uint32_t slotIndex = 0;
    uint32_t mergeBlock = 0;
    LLVMTypeKind typeKind = LLVMTypeKind::Float;
    std::vector<std::pair<uint32_t, IRValue>> incomingValues; // (predBlock, val)
};

/**
 * @brief Instrumentation record connecting each PHI incoming edge to its emitted DXBC assignment.
 */
struct PHIEdgeAssignmentRecord {
    uint32_t phiSsa = 0;
    uint32_t slotIndex = 0;
    uint32_t mergeBlock = 0;
    uint32_t predecessorBlock = 0;
    uint32_t destTempReg = 0;
    IRValue incomingValue;
    bool isAssignmentEmitted = false;
    std::string assignmentDisasm;
    uint32_t splitBlockId = 0;
    bool isSplitEdge = false;
    uint32_t incomingSsa = 0;
    std::string branchEdgeDescription;
};

/**
 * @brief Verification record for loop-header PHI nodes inserted for loop-carried variables.
 */
struct LoopPhiRecord {
    uint32_t slotIndex = 0;
    uint32_t phiSsa = 0;
    uint32_t phiTempReg = 0;
    uint32_t preheaderBlock = 0;
    uint32_t headerBlock = 0;
    uint32_t latchBlock = 0;
    uint32_t backEdgeDestBlock = 0;
    uint32_t loopExitBlock = 0;
    IRValue initialValue;
    IRValue latchIncomingValue;
    IRValue exitReachingDef;
    uint32_t splitLandingBlock = 0;
    bool isBackEdgeSplit = false;
};

/**
 * @brief Detailed metrics and verification results from the Mem2Reg optimization pass.
 */
struct Mem2RegStats {
    uint32_t allocationsDiscovered = 0;
    uint32_t allocationsEligible = 0;
    uint32_t allocationsPromoted = 0;
    uint32_t allocationsUnpromoted = 0;
    uint32_t phisInserted = 0;
    uint32_t redundantPhisRemoved = 0;
    uint32_t criticalEdgesSplit = 0;
    uint32_t loopInvariantAllocationsPromoted = 0;
    uint32_t loopCarriedAllocationsPromoted = 0;
    uint32_t loopHeaderPhisInserted = 0;
    uint32_t backEdgeAssignmentsEmitted = 0;
    uint32_t loadsReplaced = 0;
    uint32_t storesEliminated = 0;
    bool isVerified = false;
    std::string verificationDiagnostic;

    std::vector<PromotedLoadRecord> promotedLoads;
    std::vector<InsertedPhiRecord> insertedPhis;
    std::vector<PHIEdgeAssignmentRecord> phiEdgeAssignments;
    std::vector<LoopPhiRecord> loopPhis;
};

/**
 * @brief Configuration flags for the Mem2Reg optimization pass.
 */
struct Mem2RegOptions {
    bool enabled = true;
    bool verifySSA = true;
    bool splitCriticalEdges = true;
    bool promoteLoops = true;
};

/**
 * @brief Natural loop topology and classification for bounded loop Mem2Reg.
 */
struct NaturalLoopInfo {
    bool isNaturalLoop = false;
    bool isCanonical = false;
    uint32_t header = 0;
    uint32_t preheader = 0;
    uint32_t latch = 0;
    uint32_t exitingBlock = 0;
    uint32_t exitBlock = 0;
    std::pair<uint32_t, uint32_t> backEdge = { 0, 0 };
    std::set<uint32_t> loopBlocks; // all blocks in natural loop
    std::string rejectionReason;
};

/**
 * @brief Full Control-Flow Graph (CFG) analysis, reachability, cycle detection,
 *        and dominator/dominance frontier calculations for bounded DXIL IR.
 */
struct CFGAnalysis {
    uint32_t numBlocks = 0;
    uint32_t entryBlock = 0;
    std::set<uint32_t> reachableBlocks;
    std::map<uint32_t, std::vector<uint32_t>> preds;
    std::map<uint32_t, std::vector<uint32_t>> succs;
    std::vector<std::pair<uint32_t, uint32_t>> criticalEdges;
    bool isConsistent = true;
    bool hasCycle = false;
    bool hasNaturalLoop = false;
    NaturalLoopInfo loopInfo;
    bool isIrreducible = false;
    bool exceedsBlockLimit = false; // > 16 reachable blocks
    std::string diagnostic;

    // Dominance
    std::map<uint32_t, std::set<uint32_t>> dom; // b -> set of blocks that dominate b
    std::map<uint32_t, uint32_t> idom;          // b -> immediate dominator of b (for b != entry)
    std::map<uint32_t, std::vector<uint32_t>> domTreeChildren; // b -> children in dom tree
    std::map<uint32_t, std::set<uint32_t>> domFrontier;       // b -> dominance frontier DF(b)

    static bool Analyze(const DXILIntermediateRepresentation& ir, CFGAnalysis& outCFG);
    static std::set<uint32_t> ComputeIDF(const CFGAnalysis& cfg, const std::set<uint32_t>& defBlocks);
    static bool SplitCriticalEdge(DXILIntermediateRepresentation& ir, CFGAnalysis& cfg, uint32_t u, uint32_t v, uint32_t& outSplitBlock);
};

/**
 * @brief Dedicated, conservative Memory-to-Register (Mem2Reg) optimization pass for DXIL IR.
 *        Supports both single-block scalar allocations and acyclic multi-block control-flow
 *        patterns (branches and merge blocks) using dominance-frontier PHI insertion and SSA renaming.
 */
class DXILMem2Reg {
public:
    /**
     * @brief Executes the conservative bounded Mem2Reg pass on the given IR.
     */
    static bool Run(
        DXILIntermediateRepresentation& ir,
        const Mem2RegOptions& options,
        Mem2RegStats& outStats,
        DXILTranslationStatus& outStatus,
        std::string& outDiagnostic);

    /**
     * @brief Checks if a specific local allocation meets all conservative eligibility criteria.
     */
    static bool IsEligible(
        const DXILIntermediateRepresentation& ir,
        uint32_t slotIdx,
        std::string* pReason = nullptr);

    /**
     * @brief Internal helper to check eligibility with an already computed CFG analysis.
     */
    static bool IsEligibleWithCFG(
        const DXILIntermediateRepresentation& ir,
        const CFGAnalysis& cfg,
        uint32_t slotIdx,
        std::string* pReason = nullptr);
};

} // namespace kepler
