//

#include <string.h>
#include <omnetpp.h>
#include "pbflow_m.h"
#include <algorithm> // std::min
#include <deque>

using namespace omnetpp;

//CHANGES MADE: changed 0.2 to 0.1 --> continuous and simultaneous receiving and forwarding packets
//CHANGES MADE: delay, average delay, buffer utilisation and link utilisation can now be measured
//              on top of price
//CHANGES MADE: changed AGAINx2 supply to (supply + link_rate) --> to account for packets that can be serviced immediately
//CHANGES MADE: loss%, avg_delay, QoS is now based on the 0.1 second interval cycle, not all past data
//CHANGES MADE: 5 users sending to 1 --> now 8 users sending to 1
//CHANGES MADE: receiving --> supply + link_rate   ....   price change now based on link_rate
//CHANGES MADE: price update BEFORE forwarding messages (and decreasing demand/buffer)
//CHANGES MADE: middle node colour system
//      Blue / 0% link utilisation --> White / 100% link utilisation --> Red / 100% link utilisation and queue/buffer full
//CHANGES MADE: can't receive partial packets anymore, but can send out partial packets
//CHANGES MADE: changed budget rates, etc. (adjusted for 8 senders, divided by 10 for 0.1 sec)
//CHANGES MADE: once packet stream stops, use "new_stream_chance"% (e.g. 5%) to re-transmit new sized packets
//CHANGES MADE: tic goes 'white' when not transmitting anything
//CHANGES MADE: renamed tic1 and node1 to user and switch
//CHANGES MADE: fixed middle switch going a weird half-half colour when blue sometimes
//CHANGES MADE: added minimum of 1kb for bm and stream_size
//CHANGES MADE: price printed is still $/GB/sec but simulation is now $1000s/GB/0.1 sec
//CHANGES MADE: NEW IDEA: 'c' should be a fraction of 'price' so it is scalable
//CHANGES MADE: bug fix -- changed 'buffer' to 'demand' in blue colour update section
//      Sometimes the simulation wouldn't display red when the packet was bigger than the buffer
//      i.e. packet dropped, but buffer unused
//      this now has been fixed
//CHANGES MADE: instances of 'pbflowMsg' changed to 'kB' and changed sprintf inputs accordingly
//CHANGES MADE: fixed/changed stream_size ... from == 1 ... to = 1
//CHANGES MADE: added size_increase to increase the size of all packets by the same amount (demand)
//CHANGES MADE: partial packets receivable again
//CHANGES MADE: fixed a buffer_QoS * buffer_bytes mismatch (queues not same size) problem so qos_count wasn't accurate
//CHANGES MADE: dropped packets now give a QoS of 1
//CHANGES MADE: added budget_increase to increase the budget rates of all users by the same amount

//NOTE: rebuilding network can break the simulation -- just close and re-run (unless I find a fix)

//CAN DO: maybe make another simulation to show how users interact with the NB
//an informative one, to show the methodology rather than simulate numbers and results
//Conversely, this simulation right here is one to gauge outputs/results

//CAN DO:
//trying to make Toc1 (the destination) a separate 'entity' but getting errors
//Can revert to it being a 'tic' and adding 'if' statements in the handle section to delete
//msgs when they arrive -- or continue trying to do this so i have separate sections of code
//to handle the destination

//CAN DO: QoS measure is currently batch-calculated same time price is updated -- can make it update per packet

//NOTE: Packets in reality are very small, e.g. 64 bytes, 576 bytes, 1536 bytes

//TO DO: Simulate 100%chance for different 'c' values and can analyse standard deviations and 100% settling times

//TO DO: Do more simulations around a = 0.9 to find the best 'a' value

//SHOULD DO: make initial price a controllable variable -- so I don't need to manually change in the code

//Currently -- buffer fills and price drops ... then price settles based on some combination of link_rate and supply
//Idea -- make 'supply' (in the price equation) be link_rate
//     -- buffer will be separate and will probably just help with packet_loss when congestion spikes up
//     -- demand will be based on packets sent as normal
//     -- adjust price formula -- so price will lower to reduce buffer build-up and increase to settle once buffer empties
//     -- this can probably be done with an extra term ... -x*buffered_kb ... where 'x' is a variable coefficient
//NOTE: this idea has been dropped

//CAN DO: Add 'buffer' to the sending/receiving nodes?

//Defining double-ended queues to store data
std::deque<double>buffer_bytes;
std::deque<double>buffer_bytes_temp;
std::deque<double>buffer_QoS;
std::deque<double>delay;
std::deque<double>delay_temp;

//Defining signals to emit outputs to a text file
simsignal_t priceSignalId = cComponent::registerSignal("price");
simsignal_t delaySignalId = cComponent::registerSignal("delay"); //a vector of delays
simsignal_t avg_delaySignalId = cComponent::registerSignal("avg_delay"); // [delay*kbytes])/kbytes
simsignal_t link_utilisationSignalId = cComponent::registerSignal("link_utilisation");
simsignal_t buffer_utilisationSignalId = cComponent::registerSignal("buffer_utilisation");
simsignal_t kb_lostSignalId = cComponent::registerSignal("kb_lost");
simsignal_t loss_percentageSignalId = cComponent::registerSignal("loss_percentage");
simsignal_t QoSSignalId = cComponent::registerSignal("QoS");

double demand;
double buffer;
double price;

double w[8];

double bm[8];
double bmin[8];
double bmax[8];
double ur1[8];

double qos_count; // running total of QoS * kbytes to measure output effectiveness/efficiency
double total_bytes; // running total of total kbytes sent to measure output effectiveness/efficiency (Note kbytes, not bytes)
double kb_in_cycle; // kbytes going to middle node in the cycle (0.1 second interval)
double delay_count; //running sum of delay * kbytes to calculate average delay (divide it by total_bytes)
double kb_lost; //running sum of kbytes from lost/dropped packets to calculate %loss (divide by total_bytes)
double stream[8]; // use to determine if stream of packets is continuous or not
double stream_size[8]; // the constant packet size (with small jitter / std dev)
double stream_type[8]; // the packet type (MoD or tele) when stream is continuous

//These will probably be inputs that we can change from simulation to simulation via 'versions' in omnetpp.ini???
//Right now they are initialised with values in void Tic1::initialize() from parameter values in .ned or .ini
double supply; //buffer space?
double a; //this looks like the %utilisation we want
double c; //this looks like speed of price adjustment
double link_rate; //outgoing link rate -- this is the departure rate
double packet_chance; //0-100, %chance for packet generation (on top of 'can't affords') -- part of arrival rate
double new_stream_chance; //0-100, %chance for new stream of packets after not sending
double packet_type; //1 = both, 2 = MoD, 3 = tele
double jitter; //std dev of packet sizes
double size_increase; //to increase the size of all packets
double budget_increase; //to increase the budgets of all users
//0.1 was way too much and the price would overshoot and oscillate
//too low and it would take far too long to adjust
//right now 0.003 looks very good

/*NOTES:
 bm -> desired bandwidth (e.g. 300kb/sec)
 bmax -> how much bandwidth you can afford at that price (w/p) (e.g. 500kb/sec)
 bmin -> minimum bandwidth required to satisfy some QoS (still be on the curve)

 cyan -> MoD
 gold -> teleconferencing
*/

class Tic1 : public cSimpleModule
{
  protected:
    // The following redefined virtual function holds the algorithm.
    virtual kB *generateMessage();
    virtual void forwardMessage(kB *msg);
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

class Node1 : public cSimpleModule
{
  protected:
    // The following redefined virtual function holds the algorithm.
    virtual kB *generateMessage2();
    virtual void initialize() override;
    virtual void forwardMessage(kB *msg);
    virtual void handleMessage(cMessage *msg) override;
};

// The module class needs to be registered with OMNeT++
Define_Module(Tic1);
Define_Module(Node1);

void Tic1::initialize()
{
    // Initialize is called at the beginning of the simulation.
    //Initialise variables
    total_bytes = 0; //should be 0, but for debugging can be 1, i don't want it to say Inf when we divide by 0 at the start
    kb_in_cycle = 0;
    qos_count = 0;
    delay_count = 0;
    kb_lost = 0;
    demand = 0;
    price = 0.0001; // estimated 0.0001 // min 0.000000000001

    //Get input parameters
    a = par("a").doubleValue();
    c = par("c").doubleValue();
    link_rate = par("link_rate").doubleValue();
    packet_chance = par("packet_chance").doubleValue();
    new_stream_chance = par("new_stream_chance").doubleValue();
    packet_type = par("packet_type").doubleValue();
    supply = par("supply").doubleValue();
    jitter = par("jitter").doubleValue();
    size_increase = par("size_increase").doubleValue();
    budget_increase = par("budget_increase").doubleValue();

    //Initialise variables
    stream[0] = 1;
    stream[1] = 1;
    stream[2] = 1;
    stream[3] = 1;
    stream[4] = 1;
    stream[5] = 1;
    stream[6] = 1;
    stream[7] = 1;
    stream_size[0] = round(normal(80 + size_increase,20));
    stream_size[1] = round(normal(85 + size_increase,20));
    stream_size[2] = round(normal(90 + size_increase,20));
    stream_size[3] = round(normal(95 + size_increase,20));
    stream_size[4] = round(normal(100 + size_increase,20));
    stream_size[5] = round(normal(105 + size_increase,20));
    stream_size[6] = round(normal(110 + size_increase,20));
    stream_size[7] = round(normal(115 + size_increase,20)); //avg is 780 desired
    for (int iter = 0; iter <= 7; iter = iter + 1) {
        if (stream_size[iter] < 1) {
            stream_size[iter] = 1;
        }
    }
    if (packet_type == 1) {
        for (int iter = 0; iter <= 7; iter = iter + 1) {
            stream_type[iter] = intuniform(2,3);
        }
    }
    else { //=2 or =3
        for (int iter = 0; iter <= 7; iter = iter + 1) {
            stream_type[iter] = packet_type;
        }
    }

    //Define budget rates
    w[0] = 0.004 + budget_increase; // 40
    w[1] = 0.0055 + budget_increase; // 55
    w[2] = 0.007 + budget_increase; // 70
    w[3] = 0.0085 + budget_increase; // 85
    w[4] = 0.010 + budget_increase; // 100
    w[5] = 0.0115 + budget_increase; // 115
    w[6] = 0.013 + budget_increase; // 130
    w[7] = 0.0145 + budget_increase; // 145, can afford 740 (avg), sums to 0.074

    EV << "Price per byte is: " << price;
    if (getIndex() <= 7) {
        // Boot the process scheduling the initial message as a self-message.
        kB *msg = generateMessage();
        scheduleAt(0.0, msg); //this performs a self-message, handled (same way as normal messages) in 'handleMessage()'
        //pbflowMsg *msg2 = generateMessage();
        //scheduleAt(0.1, msg2);
        //sendDelayed(msg9, 0.1, "gate$o", 0);
        //this works too, although the messages start just about to enter the middle node ... send(msg, "gate$o", 0);
    }
    if (getIndex() == 8) {
        cDisplayString& displayString = getDisplayString();
        displayString.setTagArg("i", 1, "black");
    }
}

void Node1::initialize()
{
    while (!buffer_bytes.empty())
    {
        //This is to ensure empty queue when rebuilding simulation
        buffer_bytes.pop_front();
    }
    while (!buffer_bytes_temp.empty())
    {
        //This is to ensure empty queue when rebuilding simulation
        buffer_bytes_temp.pop_front();
    }
    while (!buffer_QoS.empty())
    {
        //This is to ensure empty queue when rebuilding simulation
        buffer_QoS.pop_front();
    }
    while (!delay.empty())
    {
        //This is to ensure empty queue when rebuilding simulation
        delay.pop_front();
    }
    while (!delay_temp.empty())
    {
        //This is to ensure empty queue when rebuilding simulation
        delay_temp.pop_front();
    }
    kB *msg = generateMessage2();
    scheduleAt(0.11, msg);
}

void Tic1::handleMessage(cMessage *msg)
{
    // The handleMessage() method is called whenever a message arrives
    // at the module.
    //send(msg, "gate$o", 0); // send out the message
    kB *ttmsg = check_and_cast<kB *>(msg);

    if (ttmsg->getDestination() == getIndex()) {
        // Message arrived.
        if (ttmsg->getDropped() == 0) {
            //EV << "Message " << ttmsg << " arrived after " << ttmsg->getHopCount() << " hops.\n";
            bubble("ARRIVED");
        } else { //redundant
            bubble("was DROPPED at node");
        }

        delete ttmsg;

    }
    else if (ttmsg->isSelfMessage() == 1) {
        if (ttmsg->getUr1() > 0) {
            if (stream[getIndex()] == 0) {
                int roll_new = intuniform(1,100);
                if (roll_new <= new_stream_chance) {
                    forwardMessage(ttmsg);
                    stream[getIndex()] = 1;

                    cDisplayString& displayString = getDisplayString();
                    if (ttmsg->getPacket_type() == 2) {
                        displayString.setTagArg("i", 1, "cyan");
                    }
                    else if (ttmsg->getPacket_type() == 3) {
                        displayString.setTagArg("i", 1, "gold");
                    }
                }
                else {
                    stream[getIndex()] = 0;
                }
            }
            else { //stream[getIndex()] == 1
                int roll = intuniform(1,100);
                if (roll <= packet_chance) {
                    forwardMessage(ttmsg);
                    stream[getIndex()] = 1;

                    cDisplayString& displayString = getDisplayString();
                    if (ttmsg->getPacket_type() == 2) {
                        displayString.setTagArg("i", 1, "cyan");
                    }
                    else if (ttmsg->getPacket_type() == 3) {
                        displayString.setTagArg("i", 1, "gold");
                    }
                }
                else {
                    cDisplayString& displayString = getDisplayString();
                    displayString.setTagArg("i", 1, "white");
                    stream[getIndex()] = 0;
                }
            }
        }
        else {
            cDisplayString& displayString = getDisplayString();
            displayString.setTagArg("i", 1, "white");
            bubble("Can't Afford");
            stream[getIndex()] = 0;
        }

        kB *msg2 = generateMessage();
        scheduleAt(simTime()+0.1, msg2);
    }
}

kB *Tic1::generateMessage()
{
    // Produce source and destination addresses.

    //pbflowMsg *msg2 = generateMessage();
    //scheduleAt(1.0, msg2);

    int src = getIndex();

    int dest = 8;
    double QoS;
    double type;

    char msgname[50];

    //Determine the type of packets each user is sending
    if (stream[src] == 1)
    {
        type = stream_type[src];
    }
    else {
        if (packet_type == 1) {
            stream_type[src] = intuniform(2,3);
            type = stream_type[src];
        }
        else {
            type = packet_type; //=2 or =3
        }
    }

    //Determine the sizing of packets from each user
    if (stream[src] == 1) {
        bm[src] = stream_size[src] + round(normal(0,jitter));
        if (bm[src] < 1) {
            bm[src] = 1;
        }
    }
    else {
        switch(src) {
        case 0:
            stream_size[src] = round(normal(80 + size_increase,20));
            break;
        case 1:
            stream_size[src] = round(normal(85 + size_increase,20));
            break;
        case 2:
            stream_size[src] = round(normal(90 + size_increase,20));
            break;
        case 3:
            stream_size[src] = round(normal(95 + size_increase,20));
            break;
        case 4:
            stream_size[src] = round(normal(100 + size_increase,20));
            break;
        case 5:
            stream_size[src] = round(normal(105 + size_increase,20));
            break;
        case 6:
            stream_size[src] = round(normal(110 + size_increase,20));
            break;
        case 7:
            stream_size[src] = round(normal(115 + size_increase,20));
            break;
        }
        if (stream_size[src] < 1) {
            stream_size[src] = 1;
        }
        bm[src] = stream_size[src] + round(normal(0,jitter));
        if (bm[src] < 1) {
            bm[src] = 1;
        }
    }

    //Determine the minimum bandwidth ratio (depending on packet type)
    if (type == 2) { //MoD
        bmin[src] = 0.6*bm[src];
    }
    else if (type == 3) { //tele
        bmin[src] = 0.15*bm[src];
    }

    bmax[src] = w[src]/price; // b is pretty much bmax if we only have 1 link

    //Determine the QoS
    if (bmax[src] > bmin[src]) {
        ur1[src] = std::min(bmax[src], bm[src]);
        if (type == 2) { //MoD
            if (ur1[src]/bm[src] > 0.9) {
                QoS = 4 + 10*(ur1[src]/bm[src] - 0.9);
            }
            else if (ur1[src]/bm[src] > 0.85) {
                QoS = 3 + 20*(ur1[src]/bm[src] - 0.85);
            }
            else { // >0.6
                QoS = 2 + 4*(ur1[src]/bm[src] - 0.6);
            }
        }
        else if (type == 3) { //tele
            if (ur1[src]/bm[src] > 0.55) {
                QoS = 4 + 2.222*(ur1[src]/bm[src] - 0.55);
            }
            else if (ur1[src]/bm[src] > 0.4) {
                QoS = 3 + 6.667*(ur1[src]/bm[src] - 0.4);
            }
            else { // >0.15
                QoS = 2 + 4*(ur1[src]/bm[src] - 0.15);
            }
        }
    }
    else {
        ur1[src] = 0;
        QoS = 1;
    }

    sprintf(msgname, ": %.0f", ur1[src]);

    // Create message object and set source and destination field.
    kB *msg = new kB(msgname);
    //the msgname inside the brackets seems to tag the sprintf above to the packet as it moves
    //Store the data into the message
    msg->setSource(src);
    msg->setDestination(dest);
    msg->setBm(bm[src]);
    msg->setBmin(bmin[src]);
    msg->setBmax(bmax[src]);
    msg->setUr1(ur1[src]);
    msg->setQoS(QoS);
    msg->setPacket_type(type);
    return msg;
}

kB *Node1::generateMessage2()
{
    kB *msg = new kB();
    msg->setUpdate_price(1); //might be redundant if we can just do self-messages
    return msg;
}

void Tic1::forwardMessage(kB *msg)
{
    // Increment hop count.
    msg->setHopCount(msg->getHopCount()+1);

    // Same routing as before: random gate.
    int n = gateSize("gate");
    int k = intuniform(0, n-1);

    //EV << "Forwarding message " << msg << " on gate[" << k << "]\n";
    send(msg, "gate$o", k);
}

void Node1::forwardMessage(kB *msg)
{
    // Increment hop count.
    msg->setHopCount(msg->getHopCount()+1);

    // Same routing as before: random gate.
    int n = gateSize("gate");
    //int k = intuniform(0, n-1);

    kB *ttmsg = check_and_cast<kB *>(msg);

    //EV << "Forwarding message " << msg << " on gate[" << ttmsg->getDestination() << "]\n";
    send(msg, "gate$o", ttmsg->getDestination());
}

void Node1::handleMessage(cMessage *msg)
{
    // The handleMessage() method is called whenever a message arrives
    // at the module.
    //send(msg, "gate$o", 0); // send out the message
    kB *ttmsg = check_and_cast<kB *>(msg);

    int src = ttmsg->getSource();
    double outgoing_bytes = 0; //note: this is actually kbytes

    //buffer full - bytes lost but still added to 'demand' to change price
    //need to remove these 'invisible' bytes after price update
    //solution used: just make demand = buffer

    if (ttmsg->isSelfMessage() == 0) { //SUS: supply vs supply+link_rate //supply settles, supply+link_rate goes a bit crazy
        //RECEIVING NEW PACKET
        if ((buffer + ttmsg->getUr1()) > (supply + link_rate)) {
            if ((supply + link_rate - buffer) > 0.001) { //==0 pretty much, but i'm scared of rounding errors
                //Buffer getting full --> do partial if room
                buffer_bytes.push_back(supply + link_rate - buffer);
                buffer = buffer + (supply + link_rate - buffer); //= supply + link_rate, pretty much
                delay.push_back(0);
                kb_lost = kb_lost + ttmsg->getUr1() - (supply + link_rate - buffer);
                buffer_QoS.push_back(ttmsg->getQoS());
                qos_count = qos_count + 1*(ttmsg->getUr1() - (supply + link_rate - buffer));
                bubble("Packets Dropped");
            }
            else {
                //Buffer/queue full --> packet completely dropped
                kb_lost = kb_lost + ttmsg->getUr1();
                qos_count = qos_count + 1*ttmsg->getUr1();
                bubble("Packets Dropped");
            }
        }
        else {
            buffer_bytes.push_back(ttmsg->getUr1());
            buffer = buffer + ttmsg->getUr1();
            delay.push_back(0);
            buffer_QoS.push_back(ttmsg->getQoS());
        }
        demand = demand + ttmsg->getUr1();
        total_bytes = total_bytes + ttmsg->getUr1();
        kb_in_cycle = kb_in_cycle + ttmsg->getUr1();
        EV << "demand: " << demand << " "; //for debugging
        EV << "buffer: " << buffer << " "; //for debugging
        //TO DO: I can make more queues to store the other bits of information
        //They might be useful for 'results'
        delete ttmsg;
    }
    else {
        //SELF-MESSAGE TO UPDATE PRICE AND FORWARD MESSAGES

        //Update Prices
        //NEW IDEA 'c'
        double new_c = price * c;
        price = price + new_c*(demand-a*link_rate)/(a*link_rate);
        if (price <= 0.000000000001) { //e-12
            price = 0.000000000001;
        }
        char price_text[50];
        sprintf(price_text, "Price: %.5f", price*1000*10); // $/GB/sec
        bubble(price_text);
        emit(priceSignalId, price*1000*10); //output to results
        //Probably could say ^, ^^, ^^^, v, vv, vvv next to the new price
        //to show increase or decrease from last iteration
        //EG: Each ^ or v can be a 0.0005 price change or under

        //Colour update to show congestion
        cDisplayString& displayString = getDisplayString();
        if ((demand - link_rate) < 0.001) { //==0 but i'm scared of rounding errors
            displayString.setTagArg("i", 1, "blue");
            double colour_val;
            if (100-demand/link_rate*100 < 0) {
                colour_val = 0;
            }
            else {
                colour_val = 100-demand/link_rate*100;
            }
            displayString.setTagArg("i", 2, colour_val);
        }
        else if (demand < (supply + link_rate - 0.001)) {
            displayString.setTagArg("i", 1, "red");
            displayString.setTagArg("i", 2, (buffer - link_rate)/supply);
        }
        else {
            displayString.setTagArg("i", 1, "red");
            displayString.setTagArg("i", 2, 100);
        }

        //Forward Messages -- need to change this to account for buffer and outgoing rate
        while (!buffer_bytes.empty() && (outgoing_bytes + 0.001) < link_rate) //+0.001 in case of rounding errors
        {
            if ((outgoing_bytes + buffer_bytes.front()) <= link_rate)
            {
                //Send out next packet in queue in full
                char msgname[50];
                sprintf(msgname, ": %.0f", buffer_bytes.front());
                kB *msg2 = new kB(msgname); //can put things inside () for label
                msg2->setUr1(buffer_bytes.front());
                msg2->setDestination(8);
                kB *newmsg2 = msg2;
                forwardMessage(newmsg2);
                outgoing_bytes = outgoing_bytes + buffer_bytes.front();
                demand = demand - buffer_bytes.front();
                buffer = buffer - buffer_bytes.front();
                qos_count = qos_count + buffer_bytes.front()*buffer_QoS.front();
                delay_count = delay_count + buffer_bytes.front()*delay.front();
                buffer_bytes.pop_front();
                buffer_QoS.pop_front();
                emit(delaySignalId,delay.front());
                delay.pop_front();
                EV << "demand: " << demand << " "; //for debugging
                EV << "buffer: " << buffer << " "; //for debugging
            }
            else {
                //Send out next packet in queue partially as the link_rate is reached
                char msgname[50];
                sprintf(msgname, ": %.0f", link_rate - outgoing_bytes);
                kB *msg2 = new kB(msgname); //can put things inside () for label
                msg2->setUr1(link_rate - outgoing_bytes);
                msg2->setDestination(8);
                kB *newmsg2 = msg2;
                forwardMessage(newmsg2);
                double remainder = buffer_bytes.front() - (link_rate - outgoing_bytes);
                demand = demand - (link_rate - outgoing_bytes);
                buffer = buffer - (link_rate - outgoing_bytes);
                qos_count = qos_count + (link_rate - outgoing_bytes)*buffer_QoS.front(); //TO DO: sus this - might need to update QoS value
                delay_count = delay_count + (link_rate - outgoing_bytes)*delay.front();
                outgoing_bytes = outgoing_bytes + (link_rate - outgoing_bytes); //=link_rate pretty much
                buffer_bytes.pop_front();
                buffer_bytes.push_front(remainder);
                emit(delaySignalId,delay.front());
                //buffer_QoS unchanged
                //delay not popped -- partial still there -- will be incremented later
                EV << "demand: " << demand << " "; //for debugging
                EV << "buffer: " << buffer << " "; //for debugging
            }
        }

        //Emit useful measures to an output text file
        emit(link_utilisationSignalId,outgoing_bytes/link_rate); //output link_utilisation
        emit(kb_lostSignalId, kb_lost);
        emit(loss_percentageSignalId, kb_lost/kb_in_cycle);
        emit(avg_delaySignalId,delay_count/(kb_in_cycle-kb_lost));
        emit(QoSSignalId,qos_count/kb_in_cycle);
        //Reset variables
        kb_lost = 0;
        kb_in_cycle = 0;
        delay_count = 0;
        qos_count = 0;

        if (abs(demand) < 0.000001) { //in case of rounding errors
            demand = 0;
        }
        if (abs(buffer) < 0.000001) { //in case of rounding errors
            buffer = 0;
        }

        emit(buffer_utilisationSignalId,buffer/supply); //output buffer_utilisation

        //Everything still in the queue needs to have delay assigned relative to size and position in queue
        double delay_var = 0;
        while (!buffer_bytes.empty()) {
            //save front value
            double bytes_temp = buffer_bytes.front();
            buffer_bytes_temp.push_back(buffer_bytes.front());

            //do useful thing with front value
            delay_var = delay_var + bytes_temp/link_rate*0.1;
            if (delay.front() == 0) {
                delay_temp.push_front(delay.front() + delay_var);
            }
            else {
                delay_temp.push_front(delay.front());
            }
            delay.pop_front();

            //grab next value
            buffer_bytes.pop_front();
        }
        //moving values from 'temp' queue back to rightful queue
        while (!buffer_bytes_temp.empty()) {
            buffer_bytes.push_back(buffer_bytes_temp.front());
            buffer_bytes_temp.pop_front();

            delay.push_back(delay_temp.front());
            delay_temp.pop_front();
        }

        demand = buffer;
        EV << "demand: " << demand << " "; //for debugging
        EV << "buffer: " << buffer << " "; //for debugging
        //will need to put the 2 below lines earlier before I clear qos_count and kb_in_cycle if I want to debug
        //EV << "qos_count: " << qos_count << " "; //for debugging
        //EV << "QoS: " << qos_count / total_bytes << " ";

        //Schedule for next time
        delete ttmsg;
        kB *msg3 = generateMessage2();
        scheduleAt(simTime()+0.1, msg3);
    }

}
