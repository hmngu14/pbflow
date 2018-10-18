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
// TODO generated message class
//
message kB {
    int source;
    int destination;
    int hopCount = 0;
    //int bytes; //redundant now
    //int priceperbyte; //probably redundant now too
    int dropped = 0; //will probably be redundant
    
    double budgetrate;
    double bm;
    double bmin;
    double bmax;
    double ur1;
    double QoS;
    double packet_type;
    
    int update_price = 0; //might be redundant if we can just check isSelfMessage();
}