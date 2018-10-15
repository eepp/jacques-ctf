/*
 * Copyright (C) 2018 Philippe Proulx <eepp.ca> - All Rights Reserved
 *
 * Unauthorized copying of this file, via any medium, is strictly
 * prohibited. Proprietary and confidential.
 */

#include "content-data-region.hpp"

namespace jacques {

static boost::optional<ByteOrder> byteOrderFromDataType(const yactfr::DataType& dataType)
{
    if (dataType.isBitArrayType()) {
        switch (dataType.asBitArrayType()->byteOrder()) {
        case yactfr::ByteOrder::BIG:
            return ByteOrder::BIG;

        case yactfr::ByteOrder::LITTLE:
            return ByteOrder::LITTLE;
        }
    }

    return boost::none;
}

ContentDataRegion::ContentDataRegion(const DataSegment& segment,
                                     const DataRange& dataRange,
                                     Scope::SP scope,
                                     const yactfr::DataType& dataType,
                                     const boost::optional<Value>& value) :
    DataRegion {
        segment,
        dataRange,
        std::move(scope),
        byteOrderFromDataType(dataType)
    },
    _dataType {&dataType},
    _value {value}
{
}

void ContentDataRegion::_accept(DataRegionVisitor& visitor)
{
    visitor.visit(*this);
}

} // namespace jacques
