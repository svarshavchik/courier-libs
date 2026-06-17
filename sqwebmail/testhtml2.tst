<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Ultimate Email Parser & Link Rewriter Validation Suite v5</title>
</head>
<body>
    <h1>Ultimate Email Parser & Link Rewriter Validation Suite v5</h1>
    <p>Your tree-based parser must strip CSS, destroy forms, block JavaScript, prevent thread freezing, and rewrite whitelisted links accurately.</p>

    <!-- 1. Harmless Protocols (Targets for your Link Rewriter) -->
    <div>
        <h2>1. Harmless Protocol Rewriting</h2>
        <p>Your parser should intercept these valid schemes and prepend or rewrite them to your safe redirect sandbox.</p>
        
        <!-- Standard HTTPS and HTTP utilizing official test domains -->
        <a id="test-https" href="https://example.com">Standard Secure HTTPS Link</a>
        <a id="test-http" href="http://example.net">Standard Unencrypted HTTP Link</a>
        
        <!-- Mailto, Tel, and News schemes -->
        <a id="test-mailto" href="mailto:support@example.org?subject=Help">Contact Support (Mailto)</a>
        <a id="test-mailto-complex" href="mailto:infosec@example.com,admin@://example.com">Complex Multi-recipient Mailto</a>
        <a id="test-tel" href="tel:+1-555-0199">Call Support (Tel)</a>
        <a id="test-news" href="news:comp.security.hololes">Usenet News Scheme</a>

        <!-- URL Encoding Tricks on Harmless Protocols -->
        <!-- URL-encoded scheme characters (Browsers or parsers may normalize this into valid URLs) -->
        <a id="test-encoded-scheme" href="%68%74%74%70%73://://example.com">Percent-Encoded HTTPS Scheme</a>
        <a id="test-encoded-slashes" href="https:%2F%2Fexample.com%2Fencoded-slashes">Percent-Encoded Slashes</a>

        <!-- Internationalized Domain Names (IDN / Punycode) -->
        <!-- Unicode domain names that your parser must handle without crashing or losing the host context -->
        <a id="test-idn-unicode" href="https://xn--exmple-cua.com">Internationalized Domain (Unicode: exämple.com)</a>
        <a id="test-idn-punycode" href="https://xn--exmple-eua.com">Internationalized Domain (Punycode: xn--exmple-eua.com)</a>
    </div>

    <!-- 2. Malicious and Edge-Case Protocol Evasions (Should NOT be rewritten as valid links) -->
    <div>
        <h2>2. Malicious and Obfuscated Link Bypasses</h2>
        <p>Your parser must strip or completely neutralize these targets. They should never resolve to a valid sandbox redirect.</p>
        
        <!-- Protocol-relative anchor link -->
        <a id="test-relative" href="//example.org/hijack">Protocol-Relative Anchor Link</a>

        <!-- Administrator bypass trick using mixed-slashes -->
        <a id="test-slashes" href="https:\\example.com\malware">Mixed Slash Link Target</a>

        <!-- Embedded active script protocol masquerading as a link -->
        <a id="test-js-uri" href="javascript:alert('Fail: Script executed')">Malicious JavaScript Link</a>
        
        <!-- HTML Entity encoded malicious scheme -->
        <a id="test-encoded-uri" href="&#x6A;&#x61;&#x76;&#x61;&#x73;&#x63;&#x72;&#x69;&#x70;&#x74;:alert('Fail')">Encoded JavaScript Link</a>
    </div>

    <!-- 3. Data-URI & Base64 Obfuscation Checks -->
    <div>
        <h2>3. Data-URI and Base64 Obfuscation</h2>
        <a href="data:text/html;base64,PHNjcmlwdD5hbGVydCgnRmFpbCcpPC9zY3JpcHQ+">Base64 Data Link (Must not execute or pass through)</a>
        <img src="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciPjxzY3JpcHQ+YWxlcnQoJ0ZhaWwnKTwvc2NyaXB0Pjwvc3ZnPg==" alt="Base64 SVG Test">
        <p style="background-image: url('data:text/css,body{background:red;}');">Data-URI CSS Check</p>
    </div>

    <!-- 4. Advanced Node Factory and Attribute Normalization -->
    <div>
        <h2>4. Advanced Node Map and Attribute Normalization</h2>
        <img src="x" onerror="console.log('Safe baseline')" onerror="alert('Fail: Second attribute prioritized')">
        <img src="x" OnErRoR="alert('Fail: Mixed case')">

        <!-- Duplicate Form Action: Testing if the parser checks the first action while the browser executes the second -->
        <form action="https://example.com" action="https://example.org">
            <input type="text" name="compromised">
        </form>
    </div>

    <!-- 5. Structural Thread Exhaustion & DoS Vectors -->
    <div>
        <h2>5. Thread Exhaustion: Sibling Proliferation</h2>
        <ul>
            <li>1</li><li>2</li><li>3</li><li>4</li><li>5</li><li>6</li><li>7</li><li>8</li><li>9</li><li>10</li>
            <li>11</li><li>12</li><li>13</li><li>14</li><li>15</li><li>16</li><li>17</li><li>18</li><li>19</li><li>20</li>
            <li>21</li><li>22</li><li>23</li><li>24</li><li>25</li><li>26</li><li>27</li><li>28</li><li>29</li><li>30</li>
        </ul>
    </div>

    <!-- 6. Content Control Group -->
    <div>
        <h2>6. Allowed Content Control Baseline</h2>
        <p>This <b>bold text</b>, <i>italic text</i>, and <u>underlined text</u> must render exactly as intended with zero active styles.</p>
    </div>
</body>
</html>
