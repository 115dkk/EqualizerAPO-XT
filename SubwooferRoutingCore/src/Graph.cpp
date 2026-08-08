// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Graph.h"

#include <utility>

namespace subroute
{

ProcessingGraph::ProcessingGraph(
	PrepareSpec prepareSpec,
	std::vector<CompiledPath> paths,
	std::vector<CompiledOutput> outputs,
	HeadroomAnalysis headroom)
	: prepareSpec_(std::move(prepareSpec)),
	  paths_(std::move(paths)),
	  outputs_(std::move(outputs)),
	  headroom_(std::move(headroom))
{
}

const PrepareSpec& ProcessingGraph::prepareSpec() const noexcept
{
	return prepareSpec_;
}

const std::vector<CompiledPath>& ProcessingGraph::paths() const noexcept
{
	return paths_;
}

const std::vector<CompiledOutput>& ProcessingGraph::outputs() const noexcept
{
	return outputs_;
}

const HeadroomAnalysis& ProcessingGraph::headroom() const noexcept
{
	return headroom_;
}

}
