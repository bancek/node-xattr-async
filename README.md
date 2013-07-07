# xattr-async

Async library for getting and setting extended attributes on files.

## Install

    $ npm install xattr-async

## API

### xattr.list(path, callback(err, listOfAttrs))

List of attributes (names).

### xattr.get(path, name, callback(err, attr))

Get attribute.

### xattr.set(path, name, value, callback(err))

Set attribute.

### xattr.remove(path, name, callback(err))

Remove attribute.

### xattr.llist(path, callback(err, listOfAttrs))

Like `list`, except it doesn't follow symlinks.

### xattr.lget(path, name, callback(err, attr))

Like `get`, except it doesn't follow symlinks.

### xattr.lset(path, name, value, callback(err))

Like `set`, except it doesn't follow symlinks.

### xattr.lremove(path, name, callback(err))

Like `remove`, except it doesn't follow symlinks.

## Running tests

    $ npm test

## Authors

Crafted by highly motivated engineers at [Koofr](http://koofr.net) and, hopefully, making your day just a little bit better.
