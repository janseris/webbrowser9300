['Hello, World'.toUpperCase(), 'a-b-c'.split('-').reverse().join('+'), 'abcdef'.substring(2, 4), 'abcdef'.slice(-2), '  x '.replace(/^\s+|\s+$/g, ''),
 'x'.charCodeAt(0), String.fromCharCode(269), encodeURIComponent('a b&č'), decodeURIComponent('%C5%A1%20'), 'abc'.indexOf('c'),
 'a,b,,c'.split(',').length, 'Aa'.toLowerCase(), 'ab'.concat('cd', 1), 'xyz'.charAt(1), 'žluťoučký'.length].join('|')
