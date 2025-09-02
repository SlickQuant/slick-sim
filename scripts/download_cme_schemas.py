#!/usr/bin/env python3
"""
Script to download official CME Group SBE schemas from their public FTP site.

This script downloads the latest SBE schema files from CME Group's public FTP server
and places them in the appropriate directory for use with our exchange simulator.

Note: For production access to CME schemas, you need to complete CME's certification
process and use their SFTP site (sftpng.cmegroup.com).
"""

import urllib.request
import urllib.error
import os
import sys
from pathlib import Path

# CME Group public FTP URLs for SBE schemas
CME_FTP_BASE = "https://www.cmegroup.com/ftp"

# Schema files to download (publicly available)
SCHEMA_FILES = {
    "templates_FixBinary.xml": f"{CME_FTP_BASE}/SBEFix/Production/Templates/templates_FixBinary.xml",
    "templates_FixBinary_UAT.xml": f"{CME_FTP_BASE}/SBEFix/UAT/Templates/templates_FixBinary.xml",
    # Alternative paths to try
    "templates_alt1.xml": f"{CME_FTP_BASE}/pub/SBEFix/Production/Templates/templates_FixBinary.xml",
    "templates_alt2.xml": f"{CME_FTP_BASE}/SBEFix/Templates/templates_FixBinary.xml",
}

# Fallback: Create a minimal CME-compatible schema if downloads fail
FALLBACK_SCHEMA = '''<?xml version="1.0" encoding="UTF-8"?>
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="cme.sbe"
                   id="8"
                   version="9" 
                   semanticVersion="9.0"
                   description="CME iLink3 Compatible Schema"
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
    
    <sbe:message name="NewOrderSingle" id="100" description="New Order Single">
        <field name="ClOrdID" id="11" type="char" length="20"/>
        <field name="Side" id="54" type="SideReq"/>
        <field name="OrderQty" id="38" type="QTY"/>
        <field name="OrdType" id="40" type="OrdTypeReq"/>
        <field name="Price" id="44" type="PRICE"/>
        <field name="SecurityID" id="48" type="int32"/>
        <field name="SendingTimeEpoch" id="5297" type="uint64"/>
    </sbe:message>
    
    <sbe:message name="ExecutionReport" id="150" description="Execution Report">
        <field name="ClOrdID" id="11" type="char" length="20"/>
        <field name="ExecID" id="17" type="char" length="40"/>
        <field name="Side" id="54" type="SideReq"/>
        <field name="OrderQty" id="38" type="QTY"/>
        <field name="LastQty" id="32" type="QTY"/>
        <field name="Price" id="44" type="PRICE"/>
        <field name="SecurityID" id="48" type="int32"/>
        <field name="SendingTimeEpoch" id="5297" type="uint64"/>
    </sbe:message>
</sbe:messageSchema>'''

def download_file(url: str, destination: Path, timeout: int = 10) -> bool:
    """Download a file from URL to destination path with timeout and retries."""
    max_retries = 3
    
    for attempt in range(max_retries):
        try:
            if attempt > 0:
                print(f"  Retry {attempt}/{max_retries} for {url}")
            else:
                print(f"Downloading {url} (timeout: {timeout}s)...")
            
            # Create request with timeout and user agent
            request = urllib.request.Request(url)
            request.add_header('User-Agent', 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36')
            
            with urllib.request.urlopen(request, timeout=timeout) as response:
                # Read in chunks to avoid memory issues with large files
                content = bytearray()
                chunk_size = 8192
                while True:
                    chunk = response.read(chunk_size)
                    if not chunk:
                        break
                    content.extend(chunk)
                    print(f"  Downloaded {len(content):,} bytes...", end='\r')
                
            print()  # New line after progress
            
            with open(destination, 'wb') as f:
                f.write(content)
                
            print(f"✓ Downloaded {destination.name} ({len(content):,} bytes)")
            return True
            
        except urllib.error.HTTPError as e:
            print(f"✗ HTTP Error {e.code}: {e.reason}")
            if e.code == 403:
                print("  CME may be restricting public access to this file")
            elif e.code == 404:
                print("  File not found - CME may have moved this resource")
            break  # Don't retry HTTP errors
            
        except urllib.error.URLError as e:
            print(f"✗ URL Error: {e.reason}")
            if "timeout" in str(e.reason).lower():
                print(f"  Connection timeout after {timeout}s")
                if attempt < max_retries - 1:
                    print(f"  Will retry with longer timeout...")
                    timeout = min(timeout * 2, 60)  # Increase timeout, max 60s
                    continue
            break
            
        except Exception as e:
            print(f"✗ Error downloading: {e}")
            if attempt < max_retries - 1:
                print("  Retrying...")
                continue
            break
    
    return False

def main():
    """Main function to download CME schemas."""
    # Determine script directory and target directory
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    schemas_dir = project_root / "src" / "order_gateway" / "sbe" / "schemas" / "cme_official"
    
    # Create target directory
    schemas_dir.mkdir(parents=True, exist_ok=True)
    
    print("CME Group SBE Schema Downloader")
    print("=" * 40)
    print(f"Target directory: {schemas_dir}")
    
    # Check if primary schema already exists
    primary_schema = schemas_dir / "templates_FixBinary.xml"
    if primary_schema.exists():
        print(f"✓ Schema already exists: {primary_schema}")
        print("Use --force to re-download")
        return 0
    
    print()
    print("⚠️  Note: CME Group may restrict public FTP access")
    print("   If downloads fail, a fallback CME-compatible schema will be created")
    print()
    
    success_count = 0
    total_files = len(SCHEMA_FILES)
    
    for filename, url in SCHEMA_FILES.items():
        destination = schemas_dir / filename
        if download_file(url, destination, timeout=15):
            success_count += 1
        print()  # Empty line for readability
        
        # If we got the primary file, we can stop
        if filename == "templates_FixBinary.xml" and success_count > 0:
            print("✓ Got primary schema file - skipping alternatives")
            break
    
    print("Download Summary")
    print("=" * 40)
    print(f"Successfully downloaded: {success_count}/{total_files} files")
    
    # If no files downloaded, create fallback schema
    primary_schema = schemas_dir / "templates_FixBinary.xml"
    if success_count == 0 and not primary_schema.exists():
        print()
        print("⚠️  No schemas downloaded - creating CME-compatible fallback schema")
        try:
            with open(primary_schema, 'w', encoding='utf-8') as f:
                f.write(FALLBACK_SCHEMA)
            print(f"✓ Created fallback schema: {primary_schema}")
            print()
            print("🔧 Using minimal CME-compatible schema for development")
            print("📋 Contains basic NewOrderSingle and ExecutionReport messages")
        except Exception as e:
            print(f"✗ Failed to create fallback schema: {e}")
            return 1
    
    if success_count > 0 or primary_schema.exists():
        print(f"Schema files are available in: {schemas_dir}")
        print()
        print("Next steps:")
        print("1. Build the project: cmake -S . -B build && cmake --build build")
        print("2. Generated C++ classes will be in build/sbe_generated/")
        print()
        if success_count == 0:
            print("📝 Note: Using fallback schema - for production use, obtain")
            print("   official schemas from CME after certification process.")
        else:
            print("📝 Note: For production use, obtain certified schemas from")
            print("   CME Group via SFTP (sftpng.cmegroup.com).")
        return 0
    else:
        print("Failed to obtain any schema files.")
        return 1

if __name__ == "__main__":
    sys.exit(main())