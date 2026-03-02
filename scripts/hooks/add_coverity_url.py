#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""
commit-msg hook script to add Coverity query URLs after CID blocks in commit messages.

This script parses commit messages for Coverity IDs (CIDs) and automatically adds
a query URL to view those defects in Coverity.

Usage:
    As a commit-msg hook: This script receives the commit message file path as argument.
    Standalone: python add_coverity_url.py <commit_message_file>
"""

import re
import sys
from typing import List, Tuple


COVERITY_BASE_URL = "https://coverity.geo.arm.com/query/defects.htm"
STREAM_NAME = "ASTL-main"


def extract_cids_from_block(lines: List[str], start_idx: int) -> Tuple[List[str], int, bool]:
    """
    Extract CIDs from a block starting at start_idx.
    
    Args:
        lines: List of lines from the commit message
        start_idx: Index where the CID block starts
        
    Returns:
        Tuple of (list of CIDs, index where CID block ends, has_url_already)
    """
    cids = []
    current_idx = start_idx
    has_url = False
    
    # Check if CIDs are on the same line as the CID marker
    first_line = lines[start_idx]
    # Match "CID: 12345" or "cid 12345, 67890" patterns
    if ':' in first_line:
        cid_text = first_line.split(':', 1)[1]
    else:
        parts = first_line.split(None, 1)
        if len(parts) > 1:
            cid_text = parts[1]
        else:
            cid_text = ''
    same_line_cids = re.findall(r'\d+', cid_text)
    cids.extend(same_line_cids)
    
    # Continue to next lines to find more CIDs
    current_idx += 1
    while current_idx < len(lines):
        line = lines[current_idx].strip()
        
        # Stop if we hit a blank line
        if not line:
            break
        
        # Check if this line is already a Coverity URL
        if re.search(r'https?://\S*coverity\S*', line, re.IGNORECASE):
            has_url = True
            current_idx += 1
            break
            
        # Extract all numbers from the line (handles comma-separated or space-separated)
        found_cids = re.findall(r'\d+', line)
        
        # If line has numbers, assume they're CIDs
        if found_cids:
            cids.extend(found_cids)
            current_idx += 1
        else:
            # If line has no numbers but has text, stop collecting CIDs
            break
    
    return cids, current_idx, has_url


def generate_coverity_url(cids: List[str]) -> str:
    """
    Generate a Coverity query URL for the given CIDs.
    
    Args:
        cids: List of Coverity IDs
        
    Returns:
        Formatted URL string
    """
    if not cids:
        return ""
    
    cid_params = "&".join(f"cid={cid}" for cid in cids)
    return f"{COVERITY_BASE_URL}?{cid_params}&stream={STREAM_NAME}"


def process_commit_message(message: str) -> str:
    """
    Process commit message and add Coverity URLs after CID blocks.
    
    Args:
        message: Original commit message
        
    Returns:
        Modified commit message with Coverity URLs added
    """
    lines = message.split('\n')
    result_lines = []
    i = 0
    
    while i < len(lines):
        line = lines[i]
        result_lines.append(line)
        
        # Check if this line starts a CID block (case-insensitive)
        if re.match(r'^\s*cid\s*:?\s*\d*', line, re.IGNORECASE):
            # Extract CIDs from this block
            cids, end_idx, has_url = extract_cids_from_block(lines, i)
            
            # Add remaining lines from the CID block
            for j in range(i + 1, end_idx):
                result_lines.append(lines[j])
            
            # Generate and add the Coverity URL only if one doesn't exist already
            if cids and not has_url:
                url = generate_coverity_url(cids)
                aliased_url = f"[View in Coverity]({url})"
                result_lines.append(aliased_url)
            
            # Continue from after the CID block
            i = end_idx
        else:
            i += 1
    
    return '\n'.join(result_lines)


def main():
    """Main entry point for the script."""
    if len(sys.argv) < 2:
        print("Usage: add_coverity_url.py <commit_message_file>", file=sys.stderr)
        sys.exit(1)
    
    commit_msg_file = sys.argv[1]
    
    # Read the commit message
    try:
        with open(commit_msg_file, 'r', encoding='utf-8') as f:
            original_message = f.read()
    except Exception as e:
        print(f"Error reading commit message file: {e}", file=sys.stderr)
        sys.exit(1)
    
    # Process the message
    modified_message = process_commit_message(original_message)
    print(f"Modified commit message:\n{modified_message}")
    
    # Write back only if changes were made
    if modified_message != original_message:
        try:
            with open(commit_msg_file, 'w', encoding='utf-8') as f:
                f.write(modified_message)
        except Exception as e:
            print(f"Error writing commit message file: {e}", file=sys.stderr)
            sys.exit(1)


if __name__ == "__main__":
    main()
