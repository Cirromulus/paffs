#include <cmath>

namespace paffs{
	static_assert(blocksPerArea >= 2, "At least 2 blocks per area are required");
	static_assert(blocksTotal >= 8, "At least 8 Blocks are needed to function properly");
	static_assert(mramSize % 512 == 0, "Mram Size should be a multiple of 512 for mram Viewer");
	static_assert(treeNodeCacheSize >= 2, "At least two tree Nodes have to be cacheable");
	static_assert(areaSummaryCacheSize >= 3, "At least three areas have to be cacheable");
	static_assert(maxNumberOfInodes >= 2, "At least two inodes may be open simultaneously");
	static_assert(maxNumberOfFiles >= 1, "At least one file may be open simultaneously");

	//automatically calculated
	static constexpr uint16_t totalBytesPerPage = dataBytesPerPage + oobBytesPerPage;
	static constexpr uint16_t areasNo = blocksTotal / blocksPerArea;
	static constexpr uint16_t totalPagesPerArea = blocksPerArea * pagesPerBlock;
	//minimum one byte with one bit per page in an area divided by page width
	static constexpr uint16_t oobPagesPerArea = std::ceil(((blocksPerArea * pagesPerBlock) / 8. / dataBytesPerPage));
			;
	static constexpr uint16_t dataPagesPerArea = totalPagesPerArea - oobPagesPerArea;
	//minimum one byte with one bit per page, and another byte for meta
	static constexpr uint16_t areaSummarySize = 1 + dataPagesPerArea / 8 + 1;
	static constexpr uint16_t superChainElems = jumpPadNo + 2;

	static constexpr uint16_t addrsPerPage = dataBytesPerPage / sizeof(Addr);
	static constexpr uint16_t minFreeAreas = 1;

	static constexpr uint16_t journalTopicLogSize = 500;
}
