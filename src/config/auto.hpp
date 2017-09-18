namespace paffs{
	//automatically calculated
	static constexpr uint16_t totalBytesPerPage = dataBytesPerPage + oobBytesPerPage;
	static constexpr uint16_t areasNo = blocksTotal / blocksPerArea;
	static constexpr uint16_t totalPagesPerArea = blocksPerArea * pagesPerBlock;
	//minimum one byte with one bit per page in an area divided by page width
	static constexpr uint16_t  oobPagesPerArea = 1 +
			((blocksPerArea * pagesPerBlock) / 8  / dataBytesPerPage);
	static constexpr uint16_t dataPagesPerArea = totalPagesPerArea - oobPagesPerArea;
	//minimum one byte with one bit per page, and another byte for meta
	static constexpr uint16_t areaSummarySize = 1 + dataPagesPerArea / 8 + 1;
	static constexpr uint16_t superChainElems = jumpPadNo + 2;
	//Address buffer for dataIO, currently only first indirection is implemented
	static constexpr uint16_t addrsPerPage = dataBytesPerPage / sizeof(Addr);
	static constexpr uint16_t minFreeAreas = 1;

}