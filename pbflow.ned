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
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

//
// This file is part of an OMNeT++/OMNEST simulation example.
//
// Copyright (C) 2003-2015 Andras Varga
//
// This file is distributed WITHOUT ANY WARRANTY. See the file
// `license' for details on this and other legal matters.
//


simple Tic1
{
    parameters:
        @display("i=block/process");
        double a = default(0.9);
        double c = default(0.05);
        double supply = default(150);
        double link_rate = default(450);
        double packet_chance = default(97);
        double new_stream_chance = default(40);
        double packet_type = default(1); //1 = both, 2 = MoD, 3 = tele
        double jitter = default(2);
        double size_increase = default(0);
        double budget_increase = default(0);
    gates:
        inout gate[];
}

simple Node1
{
    parameters:
        @display("i=block/routing");
        @signal[price](type=double);
        @statistic[price](source=price; record=vector);
        @signal[delay](type=double);
        @statistic[delay](source=delay; record=vector);
        @signal[avg_delay](type=double);
        @statistic[avg_delay](source=avg_delay; record=vector);
        @signal[link_utilisation](type=double);
        @statistic[link_utilisation](source=link_utilisation; record=vector);
        @signal[buffer_utilisation](type=double);
        @statistic[buffer_utilisation](source=buffer_utilisation; record=vector);
        @signal[kb_lost](type=double);
        @statistic[kb_lost](source=kb_lost; record=vector);
        @signal[loss_percentage](type=double);
        @statistic[loss_percentage](source=loss_percentage; record=vector);
        @signal[QoS](type=double);
        @statistic[QoS](source=QoS; record=vector);
    gates:
        inout gate[];
}

network pbflow
{
    types:
        channel Channel extends ned.DelayChannel
        {
            delay = 100ms;
        }
    submodules:
        user[9]: Tic1 {
            parameters:
                @display("i=,white");
        }
        //toc[3]: Tic1 {
        //    parameters:
        //        @display("i=,grey");
        //}
        switch: Node1 {
            parameters:
                @display("i=,red,0");
        }
    connections:

        //tic[0].out --> {  delay = 100ms; } --> node.in;
        user[0].gate++ <--> Channel <--> switch.gate++;
        user[1].gate++ <--> Channel <--> switch.gate++;
        user[2].gate++ <--> Channel <--> switch.gate++;
        user[3].gate++ <--> Channel <--> switch.gate++;
        user[4].gate++ <--> Channel <--> switch.gate++;
        user[5].gate++ <--> Channel <--> switch.gate++;
        user[6].gate++ <--> Channel <--> switch.gate++;
        user[7].gate++ <--> Channel <--> switch.gate++;
        user[8].gate++ <--> Channel <--> switch.gate++;
}

