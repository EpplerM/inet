/***************************************************************************
                          RTP.cc  -  description
                             -------------------
    (C) 2007 Ahmed Ayadi  <ahmed.ayadi@sophia.inria.fr>
    (C) 2001 Matthias Oppitz <Matthias.Oppitz@gmx.de>

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/** \file RTP.cc
 * This file contains the implementation of member functions of the class RTP.
 */

#include "IPAddress.h"
#include "UDPSocket.h"
#include "UDPControlInfo_m.h"

#include "RTP.h"
#include "RTPInnerPacket.h"
#include "RTPInterfacePacket_m.h"
#include "RTPProfile.h"

#include "RTPSenderControlMessage_m.h"
#include "RTPSenderStatusMessage_m.h"

Define_Module(RTP);


//
// methods inherited from cSimpleModule
//

void RTP::initialize()
{
    _socketFdIn = -1;//UDPSocket::generateSocketId();
    _socketFdOut = -1;
    _leaveSession = false;
    appInGate = findGate("appIn");
    profileInGate = findGate("profileIn");
    rtcpInGate = findGate("rtcpIn");
    udpInGate = findGate("udpIn");

    rcvdPkBytesSignal = registerSignal("rcvdPkBytes");
    endToEndDelaySignal = registerSignal("endToEndDelay");
}

void RTP::handleMessage(cMessage *msg)
{
    if (msg->getArrivalGateId() == appInGate)
    {
        handleMessageFromApp(msg);
    }
    else if (msg->getArrivalGateId() == profileInGate)
    {
        handleMessageFromProfile(msg);
    }
    else if (msg->getArrivalGateId() == rtcpInGate)
    {
        handleMessageFromRTCP(msg);
    }
    else if (msg->getArrivalGateId() == udpInGate)
    {
        handleMessagefromUDP(msg);
    }
    else
    {
        error("Message from unknown gate");
    }
}

//
// handle messages from different gates
//
void RTP::handleMessageFromApp(cMessage *msg)
{
    RTPControlInfo * ci = check_and_cast<RTPControlInfo *>(msg->removeControlInfo());
    delete msg;

    switch(ci->getType())
    {
    case RTP_IFP_ENTER_SESSION:
        enterSession(check_and_cast<RTPCIEnterSession *>(ci));
        break;

    case RTP_IFP_CREATE_SENDER_MODULE:
        createSenderModule(check_and_cast<RTPCICreateSenderModule *>(ci));
        break;

    case RTP_IFP_DELETE_SENDER_MODULE:
        deleteSenderModule(check_and_cast<RTPCIDeleteSenderModule *>(ci));
        break;

    case RTP_IFP_SENDER_CONTROL:
        senderModuleControl(check_and_cast<RTPCISenderControl *>(ci));
        break;

    case RTP_IFP_LEAVE_SESSION:
        leaveSession(check_and_cast<RTPCILeaveSession *>(ci));
        break;

    default:
        error("unknown RTPInterfacePacket type from application");
    }
}

void RTP::handleMessageFromProfile(cMessage *msg)
{
    RTPInnerPacket *rinp = check_and_cast<RTPInnerPacket *>(msg);

    switch(rinp->getType())
    {
    case RTP_INP_PROFILE_INITIALIZED:
        profileInitialized(rinp);
        break;

    case RTP_INP_SENDER_MODULE_CREATED:
        senderModuleCreated(rinp);
        break;

    case RTP_INP_SENDER_MODULE_DELETED:
        senderModuleDeleted(rinp);
        break;

    case RTP_INP_SENDER_MODULE_INITIALIZED:
        senderModuleInitialized(rinp);
        break;

    case RTP_INP_SENDER_MODULE_STATUS:
        senderModuleStatus(rinp);
        break;

    case RTP_INP_DATA_OUT:
        dataOut(rinp);
        break;

    default:
        error("Unknown RTPInnerPacket type %d from profile", rinp->getType());
        delete msg;
        break;
    }
    ev << "handleMessageFromProfile(cMessage *msg) Exit" << endl;
}

void RTP::handleMessageFromRTCP(cMessage *msg)
{
    RTPInnerPacket *rinp = check_and_cast<RTPInnerPacket *>(msg);

    switch(rinp->getType())
    {
    case RTP_INP_RTCP_INITIALIZED:
        rtcpInitialized(rinp);
        break;

    case RTP_INP_SESSION_LEFT:
        sessionLeft(rinp);
        break;

    default:
        error("Unknown RTPInnerPacket type %d from rtcp", rinp->getType());
        delete msg;
        break;
    }
}

void RTP::handleMessagefromUDP(cMessage *msg)
{
    readRet(msg);
}

//
// methods for different messages
//

void RTP::enterSession(RTPCIEnterSession *rifp)
{
    _profileName = rifp->getProfileName();
    _commonName = rifp->getCommonName();
    _bandwidth = rifp->getBandwidth();
    _destinationAddress = rifp->getDestinationAddress();

    _port = rifp->getPort();
    if (_port & 1)
        _port--;

    _mtu = resolveMTU();

    createProfile();
    initializeProfile();
    delete rifp;
}

void RTP::leaveSession(RTPCILeaveSession *rifp)
{
    cModule *profileModule = gate("profileOut")->getNextGate()->getOwnerModule();
    profileModule->deleteModule();
    _leaveSession = true;
    RTPInnerPacket *rinp = new RTPInnerPacket("leaveSession()");
    rinp->leaveSession();
    send(rinp,"rtcpOut");

    delete rifp;
}

void RTP::createSenderModule(RTPCICreateSenderModule *rifp)
{
    RTPInnerPacket *rinp = new RTPInnerPacket("createSenderModule()");
    ev << rifp->getSsrc()<<endl;
    rinp->createSenderModule(rifp->getSsrc(), rifp->getPayloadType(), rifp->getFileName());
    send(rinp, "profileOut");

    delete rifp;
}

void RTP::deleteSenderModule(RTPCIDeleteSenderModule *rifp)
{
    RTPInnerPacket *rinp = new RTPInnerPacket("deleteSenderModule()");
    rinp->deleteSenderModule(rifp->getSsrc());
    send(rinp, "profileOut");

    delete rifp;
}

void RTP::senderModuleControl(RTPCISenderControl *rifp)
{
    RTPInnerPacket *rinp = new RTPInnerPacket("senderModuleControl()");
    RTPSenderControlMessage * scm = new RTPSenderControlMessage();
    scm->setCommand(rifp->getCommand());
    scm->setCommandParameter1(rifp->getCommandParameter1());
    scm->setCommandParameter2(rifp->getCommandParameter2());
    rinp->senderModuleControl(rinp->getSsrc(), scm);
    send(rinp, "profileOut");

    delete rifp;
}

void RTP::profileInitialized(RTPInnerPacket *rinp)
{
    _rtcpPercentage = rinp->getRtcpPercentage();
    if (_port == PORT_UNDEF)
    {
        _port = rinp->getPort();
        if (_port & 1)
            _port--;
    }

    delete rinp;

    createSocket();
}

void RTP::senderModuleCreated(RTPInnerPacket *rinp)
{
    RTPCISenderModuleCreated* ci = new RTPCISenderModuleCreated();
    ci->setSsrc(rinp->getSsrc());
    cMessage *msg = new RTPControlMsg("senderModuleCreated()");
    msg->setControlInfo(ci);
    send(msg, "appOut");

    delete rinp;
}

void RTP::senderModuleDeleted(RTPInnerPacket *rinp)
{
    RTPCISenderModuleDeleted* ci = new RTPCISenderModuleDeleted();
    ci->setSsrc(rinp->getSsrc());
    cMessage *msg = new RTPControlMsg("senderModuleDeleted()");
    msg->setControlInfo(ci);
    send(msg, "appOut");
    // perhaps we should send a message to rtcp module
    delete rinp;
}

void RTP::senderModuleInitialized(RTPInnerPacket *rinp)
{
    send(rinp, "rtcpOut");
}

void RTP::senderModuleStatus(RTPInnerPacket *rinp)
{
    RTPSenderStatusMessage *ssm = (RTPSenderStatusMessage *)(rinp->decapsulate());
    RTPCISenderStatus* ci = new RTPCISenderStatus();
    ci->setSsrc(rinp->getSsrc());
    ci->setStatus(ssm->getStatus());
    ci->setTimeStamp(ssm->getTimeStamp());
    cMessage *msg = new cMessage("senderModuleStatus()");
    msg->setControlInfo(ci);
    send(msg, "appOut");
    delete ssm;
    delete rinp;
}

void RTP::dataOut(RTPInnerPacket *rinp)
{
    RTPPacket *msg = check_and_cast<RTPPacket *>(rinp->decapsulate());

    // send message to UDP, with the appropriate control info attached
    msg->setKind(UDP_C_DATA);

    UDPControlInfo *ctrl = new UDPControlInfo();
    ctrl->setDestAddr(_destinationAddress);
    ctrl->setDestPort(_port);
    msg->setControlInfo(ctrl);

    RTPInnerPacket *rinpOut = new RTPInnerPacket(*rinp);
    rinpOut->encapsulate(new RTPPacket(*msg));

//     ev << "Sending packet: ";msg->dump();
    send(msg, "udpOut");

    // RTCP module must be informed about sent rtp data packet

    send(rinpOut, "rtcpOut");

    delete rinp;
}

void RTP::rtcpInitialized(RTPInnerPacket *rinp)
{
    RTPCISessionEntered* ci = new RTPCISessionEntered();
    ci->setSsrc(rinp->getSsrc());
    cMessage *msg = new RTPControlMsg("sessionEntered()");
    msg->setControlInfo(ci);
    send(msg, "appOut");

    delete rinp;
}

void RTP::sessionLeft(RTPInnerPacket *rinp)
{
    RTPCISessionLeft* ci = new RTPCISessionLeft();
    cMessage *msg = new cMessage("sessionLeft()");
    msg->setControlInfo(ci);
    send(msg, "appOut");

    delete rinp;
}

void RTP::socketRet()
{
}

void RTP::connectRet()
{
    initializeRTCP();
}

void RTP::readRet(cMessage *sifp)
{
    if ( ! _leaveSession)
    {
         RTPPacket *msg = check_and_cast<RTPPacket *>(sifp);

         emit(rcvdPkBytesSignal, (long)(msg->getByteLength()));
         emit(endToEndDelaySignal, simTime()-msg->getCreationTime());

         msg->dump();
         RTPInnerPacket *rinp1 = new RTPInnerPacket("dataIn1()");
         rinp1->dataIn(new RTPPacket(*msg), IPAddress(_destinationAddress), _port);
         RTPInnerPacket *rinp2 = new RTPInnerPacket(*rinp1);
         send(rinp2, "rtcpOut");
         send(rinp1, "profileOut");
    }

    delete sifp;
}

int RTP::resolveMTU()
{
    // TODO this is not what it should be
    // do something like mtu path discovery
    // for the simulation we can use this example value
    // it's 1500 bytes (ethernet) minus ip
    // and udp headers
    return 1500 - 20 - 8;
}

void RTP::createProfile()
{
    cModuleType *moduleType = cModuleType::find(_profileName);
    if (moduleType == NULL)
        error("Profile type `%s' not found", _profileName);

    RTPProfile *profile = check_and_cast<RTPProfile *>(moduleType->create("Profile", this));
    profile->finalizeParameters();

    profile->setGateSize("payloadReceiverOut", 30);
    profile->setGateSize("payloadReceiverIn", 30);

    this->gate("profileOut")->connectTo(profile->gate("rtpIn"));
    profile->gate("rtpOut")->connectTo(this->gate("profileIn"));

    profile->callInitialize();
    profile->scheduleStart(simTime());
}

void RTP::createSocket()
{
    // TODO UDPAppBase should be ported to use UDPSocket sometime, but for now
    // we just manage the UDP socket by hand...
    if (_socketFdIn == -1)
    {
        _socketFdIn = UDPSocket::generateSocketId();

        UDPControlInfo *ctrl = new UDPControlInfo();

        IPAddress ipaddr(_destinationAddress);

        if (ipaddr.isMulticast())
        {
            ctrl->setSrcAddr(IPAddress(_destinationAddress));
            ctrl->setSrcPort(_port);
        }
        else
        {
             ctrl->setSrcPort(_port);
             ctrl->setSockId(_socketFdOut); //FIXME The next statement overwrite it!!!
        }
        ctrl->setSockId((int)_socketFdIn);
        cMessage *msg = new cMessage("UDP_C_BIND", UDP_C_BIND);
        msg->setControlInfo(ctrl);
        send(msg,"udpOut");

        connectRet();
    }
}

void RTP::initializeProfile()
{
    RTPInnerPacket *rinp = new RTPInnerPacket("initializeProfile()");
    rinp->initializeProfile(_mtu);
    send(rinp, "profileOut");
}

void RTP::initializeRTCP()
{
    RTPInnerPacket *rinp = new RTPInnerPacket("initializeRTCP()");
    int rtcpPort = _port + 1;
    rinp->initializeRTCP(_commonName, _mtu, _bandwidth, _rtcpPercentage, _destinationAddress, rtcpPort);
    send(rinp, "rtcpOut");
}
