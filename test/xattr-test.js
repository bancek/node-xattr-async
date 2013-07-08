var fs = require('fs');
var os = require('os');
var path = require('path');
var should = require('chai').should();

var xattr = require('../build/Release/xattrAsync');

describe('xattr', function() {
  var file = null;
  var nonexistingFile = null;

  beforeEach(function() {
    file = path.join(os.tmpDir(), 'xattr-test');

    nonexistingFile = path.join(os.tmpDir(), 'xattr-test-nonexisting');

    fs.writeFileSync(file, 'test');

    try {
      fs.unlinkSync(nonexistingFile);
    } catch (e) {
    }
  });

  afterEach(function() {
    fs.unlinkSync(file);
  });

  it('should list attrs', function(done) {
    xattr.set(file, 'user.foo', 'bar', function(err) {
      xattr.set(file, 'user.lorem', 'ipsum', function(err) {
        xattr.list(file, function(err, attrs) {
          attrs.length.should.equal(2);
          done();
        });
      });
    });
  });

  it('should list empty attrs', function(done) {
    xattr.list(file, function(err, attrs) {
      attrs.length.should.equal(0);
      done();
    });
  });

  it('should not list attrs for nonexisting file', function(done) {
    xattr.list(nonexistingFile, function(err, attrs) {
      should.exist(err);
      err.code.should.equal('ENOENT');
      done();
    });
  });

  it('should set attr', function(done) {
    xattr.set(file, 'user.foo', 'bar', function(err) {
      should.not.exist(err);
      done();
    });
  });

  it('should not set attr to nonexisting file', function(done) {
    xattr.set(nonexistingFile, 'user.foo', 'bar', function(err) {
      should.exist(err);
      err.code.should.equal('ENOENT');
      done();
    });
  });

  it('should get attr', function(done) {
    xattr.set(file, 'user.foo', 'bar', function(err) {
      xattr.get(file, 'user.foo', function(err, attr) {
        attr.should.equal('bar');
        done();
      });
    });
  });

  it('should not get nonexisting attr', function(done) {
    xattr.get(file, 'user.foo', function(err, attr) {
      should.exist(err);
      err.code.should.equal('ENODATA');
      done();
    });
  });

  it('should not get attr for nonexisting file', function(done) {
    xattr.get(nonexistingFile, 'user.foo', function(err) {
      should.exist(err);
      err.code.should.equal('ENOENT');
      done();
    });
  });

  it('should remove attr', function(done) {
    xattr.set(file, 'user.foo', 'bar', function(err) {
      xattr.get(file, 'user.foo', function(err, attr) {
        attr.should.equal('bar');

        xattr.remove(file, 'user.foo', function(err) {
          should.not.exist(err);

          xattr.get(file, 'user.foo', function(err) {
            should.exist(err);
            err.code.should.equal('ENODATA');
            done();
          });
        });
      });
    });
  });
});
