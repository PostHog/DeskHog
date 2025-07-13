// Safe DOM manipulation helper functions
// Use these instead of innerHTML, outerHTML, or document.write

function safeSetText(element, text) {
    if (element && typeof text === 'string') {
        element.textContent = text;
    }
}

function safeCreateElement(tagName, textContent, className) {
    const element = document.createElement(tagName);
    if (textContent) {
        element.textContent = textContent;
    }
    if (className) {
        element.className = className;
    }
    return element;
}

function safeAppendHTML(parent, htmlString) {
    // Create a temporary container
    const temp = document.createElement('div');
    temp.innerHTML = htmlString;
    
    // Move children to parent (this isolates the HTML parsing)
    while (temp.firstChild) {
        parent.appendChild(temp.firstChild);
    }
}

function escapeHTML(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}
