//
// Copyright (C) 2013 OpenSim Ltd.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "inet/mobility/contract/IMobility.h"
#include "inet/physicallayer/contract/packetlevel/IRadio.h"
#include "inet/physicallayer/contract/packetlevel/RadioControlInfo_m.h"
#include "inet/physicallayer/ieee80211/packetlevel/Ieee80211ControlInfo_m.h"
#include "inet/physicallayer/ieee80211/packetlevel/Ieee80211DimensionalTransmission.h"
#include "inet/physicallayer/ieee80211/packetlevel/Ieee80211DimensionalTransmitter.h"
#include "inet/physicallayer/ieee80211/packetlevel/Ieee80211PhyHeader_m.h"
#include "inet/physicallayer/ieee80211/packetlevel/Ieee80211Radio.h"

namespace inet {

namespace physicallayer {

Define_Module(Ieee80211DimensionalTransmitter);

Ieee80211DimensionalTransmitter::Ieee80211DimensionalTransmitter() :
    Ieee80211TransmitterBase(),
    DimensionalTransmitterBase()
{
}

void Ieee80211DimensionalTransmitter::initialize(int stage)
{
    Ieee80211TransmitterBase::initialize(stage);
    DimensionalTransmitterBase::initialize(stage);
}

std::ostream& Ieee80211DimensionalTransmitter::printToStream(std::ostream& stream, int level, int evFlags) const
{
    stream << "Ieee80211DimensionalTransmitter";
    Ieee80211TransmitterBase::printToStream(stream, level);
    return DimensionalTransmitterBase::printToStream(stream, level);
}

const ITransmission *Ieee80211DimensionalTransmitter::createTransmission(const IRadio *transmitter, const Packet *packet, simtime_t startTime) const
{
    auto phyHeader = Ieee80211Radio::peekIeee80211PhyHeaderAtFront(packet);
    const IIeee80211Mode *transmissionMode = computeTransmissionMode(packet);
    const Ieee80211Channel *transmissionChannel = computeTransmissionChannel(packet);
    W transmissionPower = computeTransmissionPower(packet);
    Hz transmissionBandwidth = transmissionMode->getDataMode()->getBandwidth();
    bps transmissionBitrate = transmissionMode->getDataMode()->getNetBitrate();
    if (transmissionMode->getDataMode()->getNumberOfSpatialStreams() > transmitter->getAntenna()->getNumAntennas())
        throw cRuntimeError("Number of spatial streams is higher than the number of antennas");
    const simtime_t duration = transmissionMode->getDuration(B(phyHeader->getLengthField()));
    const simtime_t endTime = startTime + duration;
    IMobility *mobility = transmitter->getAntenna()->getMobility();
    const Coord& startPosition = mobility->getCurrentPosition();
    const Coord& endPosition = mobility->getCurrentPosition();
    const Quaternion& startOrientation = mobility->getCurrentAngularPosition();
    const Quaternion& endOrientation = mobility->getCurrentAngularPosition();
    const Ptr<const IFunction<WpHz, Domain<simsec, Hz>>>& powerFunction = createPowerFunction(startTime, endTime, centerFrequency, transmissionBandwidth, transmissionPower);
    auto headerLength = b(transmissionMode->getHeaderMode()->getLength());
    auto dataLength = b(transmissionMode->getDataMode()->getCompleteLength(B(phyHeader->getLengthField())));
    const simtime_t preambleDuration = transmissionMode->getPreambleMode()->getDuration();
    const simtime_t headerDuration = transmissionMode->getHeaderMode()->getDuration();
    const simtime_t dataDuration = duration - headerDuration - preambleDuration;
    return new Ieee80211DimensionalTransmission(transmitter, packet, startTime, endTime, preambleDuration, headerDuration, dataDuration, startPosition, endPosition, startOrientation, endOrientation, modulation, headerLength, dataLength, centerFrequency, transmissionBandwidth, transmissionBitrate, powerFunction, transmissionMode, transmissionChannel);
}

} // namespace physicallayer

} // namespace inet

