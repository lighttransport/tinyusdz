#!/usr/bin/env python3
"""
Simple markdown-to-HTML converter for PCP API documentation.
Converts markdown files to single-page HTML with embedded CSS.
"""

import sys
import re
from pathlib import Path
from typing import List, Tuple

class MarkdownToHTML:
    def __init__(self, title: str = "PCP API Documentation"):
        self.title = title
        self.in_code_block = False
        self.code_block_language = ""
        self.current_code_buffer = []

    def convert_file(self, input_file: str, output_file: str) -> None:
        """Convert markdown file to HTML."""
        with open(input_file, 'r', encoding='utf-8') as f:
            markdown = f.read()

        html = self.convert(markdown)

        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html)

        print(f"✓ Generated {output_file}")

    def convert(self, markdown: str) -> str:
        """Convert markdown content to HTML."""
        lines = markdown.split('\n')
        html_lines = []

        i = 0
        while i < len(lines):
            line = lines[i]

            # Handle code blocks
            if line.strip().startswith('```'):
                if not self.in_code_block:
                    self.in_code_block = True
                    match = re.match(r'```(\w+)?', line.strip())
                    self.code_block_language = match.group(1) if match and match.group(1) else ""
                    self.current_code_buffer = []
                else:
                    self.in_code_block = False
                    code_content = '\n'.join(self.current_code_buffer)
                    code_content = self.escape_html(code_content)
                    if self.code_block_language == 'cpp':
                        code_content = self.highlight_cpp(code_content)
                    html_lines.append(f'<pre><code class="language-{self.code_block_language}">{code_content}</code></pre>')
                    self.current_code_buffer = []
                i += 1
                continue

            if self.in_code_block:
                self.current_code_buffer.append(line)
                i += 1
                continue

            # Skip empty lines outside code blocks
            if not line.strip():
                if html_lines and not html_lines[-1].endswith('</p>'):
                    html_lines.append('')
                i += 1
                continue

            # Process line
            processed = self.process_line(line)
            if processed:
                html_lines.append(processed)
            i += 1

        # Build complete HTML
        html_content = '\n'.join(html_lines)
        html_content = self.clean_html(html_content)

        return self.wrap_with_html(html_content)

    def process_line(self, line: str) -> str:
        """Process a single line of markdown."""
        stripped = line.strip()

        # Headers
        if stripped.startswith('#'):
            match = re.match(r'^(#{1,6})\s+(.+)$', stripped)
            if match:
                level = len(match.group(1))
                content = match.group(2).strip()
                content = self.process_inline(content)
                # Create anchor from content
                anchor = re.sub(r'[^a-z0-9]+', '-', content.lower().replace('<', '').replace('>', '').replace('`', '').replace('(', '').replace(')', ''))
                anchor = anchor.strip('-')
                return f'<h{level} id="{anchor}">{content}</h{level}>'
            return ''

        # Horizontal rules
        if stripped.startswith('---') or stripped.startswith('***') or stripped.startswith('___'):
            return '<hr>'

        # Tables
        if '|' in line:
            return self.process_table_line(line, stripped)

        # Lists
        if stripped.startswith('- ') or stripped.startswith('* '):
            return f'<li>{self.process_inline(stripped[2:])}</li>'

        if re.match(r'^\d+\.\s+', stripped):
            match = re.match(r'^(\d+)\.\s+(.+)$', stripped)
            if match:
                return f'<li>{self.process_inline(match.group(2))}</li>'

        # Regular paragraph
        if stripped:
            content = self.process_inline(stripped)
            return f'<p>{content}</p>'

        return ''

    def process_table_line(self, line: str, stripped: str) -> str:
        """Process table rows."""
        if stripped.startswith('|'):
            cells = [cell.strip() for cell in stripped.split('|')[1:-1]]

            # Check if it's a separator row
            if all(re.match(r'^[\s\-:]+$', cell) for cell in cells):
                return ''  # Skip separator

            # Determine if header or data row
            return '<tr>' + ''.join(f'<td>{self.process_inline(cell)}</td>' for cell in cells) + '</tr>'
        return ''

    def process_inline(self, text: str) -> str:
        """Process inline markdown elements."""
        # Escape HTML first
        text = self.escape_html(text)

        # Code (backticks)
        text = re.sub(r'`([^`]+)`', r'<code>\1</code>', text)

        # Bold
        text = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', text)
        text = re.sub(r'__([^_]+)__', r'<strong>\1</strong>', text)

        # Italic
        text = re.sub(r'\*([^*]+)\*', r'<em>\1</em>', text)
        text = re.sub(r'_([^_]+)_', r'<em>\1</em>', text)

        # Links
        text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', text)

        # Images
        text = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', r'<img src="\2" alt="\1">', text)

        return text

    def highlight_cpp(self, code: str) -> str:
        """Simple C++ syntax highlighting."""
        keywords = ['class', 'struct', 'enum', 'union', 'const', 'static', 'virtual',
                   'public', 'private', 'protected', 'void', 'int', 'float', 'double',
                   'bool', 'char', 'string', 'vector', 'map', 'set', 'shared_ptr',
                   'unique_ptr', 'auto', 'if', 'else', 'for', 'while', 'return', 'true', 'false']

        for keyword in keywords:
            pattern = r'\b' + keyword + r'\b'
            code = re.sub(pattern, f'<span class="kw">{keyword}</span>', code)

        return code

    def escape_html(self, text: str) -> str:
        """Escape HTML special characters."""
        return (text
                .replace('&', '&amp;')
                .replace('<', '&lt;')
                .replace('>', '&gt;')
                .replace('"', '&quot;')
                .replace("'", '&#39;'))

    def clean_html(self, html: str) -> str:
        """Clean up generated HTML."""
        # Wrap consecutive list items
        html = re.sub(r'(<li>.*?</li>\n)+', lambda m: '<ul>\n' + m.group(0) + '</ul>\n', html)

        # Wrap consecutive table rows
        html = re.sub(r'(<tr>.*?</tr>\n)+', lambda m: '<table>\n' + m.group(0) + '</table>\n', html)

        # Remove empty paragraphs
        html = re.sub(r'<p>\s*</p>', '', html)

        return html

    def wrap_with_html(self, content: str) -> str:
        """Wrap content with complete HTML structure and CSS."""
        css = self.get_simple_css()

        return f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{self.escape_html(self.title)}</title>
    <style>
{css}
    </style>
</head>
<body>
    <nav class="toc">
        <h2>Contents</h2>
        {self.generate_toc(content)}
    </nav>
    <main class="content">
{content}
    </main>
    <footer>
        <p>Generated with PCP API Documentation Tool</p>
    </footer>
</body>
</html>"""

    def generate_toc(self, html: str) -> str:
        """Generate table of contents from HTML headings."""
        headings = re.findall(r'<h(\d) id="([^"]+)">([^<]+)</h\1>', html)

        if not headings:
            return '<ul><li>No contents</li></ul>'

        toc_items = []
        current_level = 0

        for level, anchor, title in headings:
            level = int(level)

            if level > current_level:
                for _ in range(level - current_level):
                    toc_items.append('<ul>')
            elif level < current_level:
                for _ in range(current_level - level):
                    toc_items.append('</ul>')

            current_level = level
            # Clean title from HTML tags
            clean_title = re.sub(r'<[^>]+>', '', title)
            toc_items.append(f'<li><a href="#{anchor}">{clean_title}</a></li>')

        for _ in range(current_level):
            toc_items.append('</ul>')

        return '\n'.join(toc_items)

    def get_simple_css(self) -> str:
        """Get simple CSS styling."""
        return """        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            line-height: 1.6;
            color: #333;
            background: #f5f5f5;
        }

        .toc {
            position: fixed;
            left: 0;
            top: 0;
            width: 250px;
            height: 100vh;
            overflow-y: auto;
            padding: 20px;
            background: #f0f0f0;
            border-right: 1px solid #ddd;
        }

        .toc h2 {
            font-size: 1.2em;
            margin-bottom: 1em;
            color: #222;
        }

        .toc ul {
            list-style: none;
        }

        .toc li {
            margin-left: 1.2em;
            margin-bottom: 0.4em;
        }

        .toc a {
            text-decoration: none;
            color: #0066cc;
        }

        .toc a:hover {
            text-decoration: underline;
        }

        main.content {
            margin-left: 250px;
            padding: 40px;
            background: white;
            min-height: 100vh;
        }

        h1 {
            font-size: 2em;
            margin: 1.5em 0 0.5em;
            color: #222;
            border-bottom: 2px solid #0066cc;
            padding-bottom: 0.3em;
        }

        h2 {
            font-size: 1.5em;
            margin: 1.3em 0 0.4em;
            color: #333;
        }

        h3 {
            font-size: 1.2em;
            margin: 1em 0 0.3em;
            color: #444;
        }

        h4, h5, h6 {
            font-size: 1.05em;
            margin: 0.8em 0 0.2em;
            color: #555;
        }

        p {
            margin-bottom: 1em;
        }

        code {
            background: #f4f4f4;
            padding: 2px 6px;
            border-radius: 3px;
            font-family: 'Courier New', monospace;
            font-size: 0.9em;
            color: #d73a49;
        }

        pre {
            background: #f4f4f4;
            border-left: 4px solid #0066cc;
            padding: 1em;
            overflow-x: auto;
            margin: 1em 0;
            border-radius: 4px;
        }

        pre code {
            background: none;
            padding: 0;
            color: #333;
            font-size: 0.9em;
        }

        .kw {
            color: #0066cc;
            font-weight: bold;
        }

        ul, ol {
            margin-left: 2em;
            margin-bottom: 1em;
        }

        li {
            margin-bottom: 0.4em;
        }

        table {
            border-collapse: collapse;
            width: 100%;
            margin: 1em 0;
        }

        tr {
            border-bottom: 1px solid #ddd;
        }

        td {
            padding: 0.8em;
            text-align: left;
        }

        tr:nth-child(even) {
            background: #f9f9f9;
        }

        a {
            color: #0066cc;
            text-decoration: none;
        }

        a:hover {
            text-decoration: underline;
        }

        hr {
            border: none;
            border-top: 1px solid #ddd;
            margin: 2em 0;
        }

        strong {
            font-weight: bold;
            color: #222;
        }

        em {
            font-style: italic;
            color: #555;
        }

        footer {
            text-align: center;
            padding: 20px;
            background: #f0f0f0;
            border-top: 1px solid #ddd;
            font-size: 0.9em;
            color: #666;
            margin-left: 250px;
        }

        @media (max-width: 768px) {
            .toc {
                position: relative;
                width: 100%;
                height: auto;
                border-right: none;
                border-bottom: 1px solid #ddd;
                padding: 10px;
            }

            main.content {
                margin-left: 0;
                padding: 20px;
            }

            footer {
                margin-left: 0;
            }

            .toc li {
                display: inline-block;
                margin-right: 1em;
            }
        }"""


def main():
    if len(sys.argv) < 2:
        print("Usage: md2html.py <input.md> [output.html]")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else input_file.replace('.md', '.html')

    if not Path(input_file).exists():
        print(f"Error: {input_file} not found")
        sys.exit(1)

    converter = MarkdownToHTML()
    converter.convert_file(input_file, output_file)


if __name__ == '__main__':
    main()
