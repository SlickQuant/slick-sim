#!/usr/bin/env python3
"""
Quick script to create a fallback CME-compatible SBE schema.

Use this if the CME FTP download is not working or taking too long.
This creates a minimal but functional schema for development purposes.
"""

import os
import sys
from pathlib import Path

# CME iLink3 Order Entry compatible schema (based on public CME documentation)
FALLBACK_SCHEMA = '''<?xml version="1.0" encoding="UTF-8"?>
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="cme.sbe"
                   id="8"
                   version="8" 
                   semanticVersion="8.0"
                   description="CME iLink3 Order Entry Compatible Schema"
                   byteOrder="littleEndian">
    <types>
        <composite name="messageHeader" description="Standard Message Header">
            <type name="blockLength" primitiveType="uint16"/>
            <type name="templateId" primitiveType="uint16"/>
            <type name="schemaId" primitiveType="uint16"/>
            <type name="version" primitiveType="uint16"/>
        </composite>
        
        <type name="PRICE" primitiveType="int64" description="CME PRICE type"/>
        <type name="QTY" primitiveType="uint32" description="CME Quantity type"/>
        
        <enum name="SideReq" encodingType="uint8">
            <validValue name="Buy">1</validValue>
            <validValue name="Sell">2</validValue>
        </enum>
        
        <enum name="OrdTypeReq" encodingType="char">
            <validValue name="Market">1</validValue>
            <validValue name="Limit">2</validValue>
        </enum>
    </types>
    
    <!-- iLink3 NewOrderSingle (Template ID 514) -->
    <sbe:message name="NewOrderSingle" id="514" description="iLink3 New Order Single">
        <field name="ClOrdID" id="11" type="char" length="20" description="Client Order ID"/>
        <field name="PartyDetailsListReqID" id="1505" type="uint64" description="Party Details List Request ID"/>
        <field name="OrderRequestID" id="2422" type="uint64" description="Order Request ID"/>
        <field name="SendingTimeEpoch" id="5297" type="uint64" description="Sending Time Epoch"/>
        <field name="SecurityID" id="48" type="int32" description="Security ID"/>
        <field name="Side" id="54" type="SideReq" description="Side"/>
        <field name="OrderQty" id="38" type="QTY" description="Order Quantity"/>
        <field name="OrdType" id="40" type="OrdTypeReq" description="Order Type"/>
        <field name="Price" id="44" type="PRICE" presence="optional" nullValue="9223372036854775807" description="Price"/>
        <field name="StopPx" id="99" type="PRICE" presence="optional" nullValue="9223372036854775807" description="Stop Price"/>
        <field name="TimeInForce" id="59" type="uint8" description="Time in Force"/>
        <field name="ManualOrderIndicator" id="1028" type="uint8" description="Manual Order Indicator"/>
        <field name="SeqNum" id="9726" type="uint64" description="Sequence Number"/>
        <field name="SenderID" id="5392" type="char" length="20" description="Sender ID"/>
        <field name="Location" id="9537" type="char" length="5" description="Location"/>
    </sbe:message>
    
    <!-- iLink3 ExecutionReportNew (Template ID 522) -->
    <sbe:message name="ExecutionReportNew" id="522" description="iLink3 Execution Report New">
        <field name="SeqNum" id="9726" type="uint64" description="Sequence Number"/>
        <field name="UUID" id="39001" type="uint64" description="UUID"/>
        <field name="ExecID" id="17" type="char" length="40" description="Execution ID"/>
        <field name="SenderID" id="5392" type="char" length="20" description="Sender ID"/>
        <field name="ClOrdID" id="11" type="char" length="20" description="Client Order ID"/>
        <field name="PartyDetailsListReqID" id="1505" type="uint64" description="Party Details List Request ID"/>
        <field name="OrderRequestID" id="2422" type="uint64" description="Order Request ID"/>
        <field name="SendingTimeEpoch" id="5297" type="uint64" description="Sending Time Epoch"/>
        <field name="OrderID" id="37" type="uint64" description="Order ID"/>
        <field name="ExecType" id="150" type="char" description="Execution Type"/>
        <field name="OrdStatus" id="39" type="char" description="Order Status"/>
        <field name="SecurityID" id="48" type="int32" description="Security ID"/>
        <field name="Side" id="54" type="SideReq" description="Side"/>
        <field name="OrderQty" id="38" type="QTY" description="Order Quantity"/>
        <field name="LastQty" id="32" type="QTY" presence="optional" nullValue="4294967295" description="Last Quantity"/>
        <field name="LastPx" id="31" type="PRICE" presence="optional" nullValue="9223372036854775807" description="Last Price"/>
        <field name="LeavesQty" id="151" type="QTY" description="Leaves Quantity"/>
        <field name="CumQty" id="14" type="QTY" description="Cumulative Quantity"/>
        <field name="AvgPx" id="6" type="PRICE" presence="optional" nullValue="9223372036854775807" description="Average Price"/>
        <field name="OrdType" id="40" type="OrdTypeReq" description="Order Type"/>
        <field name="Price" id="44" type="PRICE" presence="optional" nullValue="9223372036854775807" description="Price"/>
        <field name="TimeInForce" id="59" type="uint8" description="Time in Force"/>
        <field name="Location" id="9537" type="char" length="5" description="Location"/>
    </sbe:message>
    
    <!-- iLink3 OrderCancelRequest (Template ID 515) -->
    <sbe:message name="OrderCancelRequest" id="515" description="iLink3 Order Cancel Request">
        <field name="ClOrdID" id="11" type="char" length="20" description="Client Order ID"/>
        <field name="OrderRequestID" id="2422" type="uint64" description="Order Request ID"/>
        <field name="ManualOrderIndicator" id="1028" type="uint8" description="Manual Order Indicator"/>
        <field name="SeqNum" id="9726" type="uint64" description="Sequence Number"/>
        <field name="SendingTimeEpoch" id="5297" type="uint64" description="Sending Time Epoch"/>
        <field name="Location" id="9537" type="char" length="5" description="Location"/>
        <field name="SecurityID" id="48" type="int32" description="Security ID"/>
        <field name="Side" id="54" type="SideReq" description="Side"/>
        <field name="OrderID" id="37" type="uint64" description="Order ID"/>
        <field name="PartyDetailsListReqID" id="1505" type="uint64" description="Party Details List Request ID"/>
        <field name="OrigClOrdID" id="41" type="char" length="20" description="Original Client Order ID"/>
        <field name="SenderID" id="5392" type="char" length="20" description="Sender ID"/>
    </sbe:message>
</sbe:messageSchema>'''

def main():
    """Create fallback CME-compatible schema."""
    # Determine script directory and target directory
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    schemas_dir = project_root / "src" / "order_gateway" / "sbe" / "schemas" / "cme_official"
    
    # Create target directory
    schemas_dir.mkdir(parents=True, exist_ok=True)
    
    schema_file = schemas_dir / "templates_FixBinary.xml"
    
    print("Creating CME-Compatible Fallback Schema")
    print("=" * 40)
    print(f"Target file: {schema_file}")
    
    try:
        with open(schema_file, 'w', encoding='utf-8') as f:
            f.write(FALLBACK_SCHEMA)
        
        print(f"[SUCCESS] Created fallback schema ({len(FALLBACK_SCHEMA):,} bytes)")
        print()
        print("iLink3 Order Entry Schema Contents:")
        print("  - NewOrderSingle message (Template ID 514)")
        print("  - ExecutionReportNew message (Template ID 522)") 
        print("  - OrderCancelRequest message (Template ID 515)")
        print("  - CME iLink3 field IDs and types")
        print("  - Schema ID: 8, Version: 8 (matches CME iLink3)")
        print()
        print("Next steps:")
        print("  1. cmake -S . -B build")
        print("  2. cmake --build build")
        print()
        print("Note: This is a minimal schema for development.")
        print("   For production, obtain official schemas from CME after certification.")
        
        return 0
        
    except Exception as e:
        print(f"[ERROR] Error creating schema file: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())