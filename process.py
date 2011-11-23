#!/usr/bin/env python
##########################################################################
#
# Copyright 2011 Jose Fonseca
# All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
##########################################################################/


import gzip
import os.path
import optparse
import re
import sys
import time
import struct
import subprocess


class GzipFile(gzip.GzipFile):

    def _read_eof(self):
        # Ignore incomplete files
        try:
            gzip.GzipFile._read_eof(self)
        except IOError:
            pass


class Context:

    def __init__(self, stamp, thread = 0, frames = ()):
        self.stamp = stamp
        self.thread = thread
        self.frames = frames


class Symbol:

    def __init__(self, addr, module, offset):
        self.addr = addr
        self.module = module
        self.offset = offset
        self._function = None

    def function(self):
        if self._function is None:
            self._function = '??'
            if self.module:
                try:
                    cmd = [
                            'addr2line',
                            '-e', self.module,
                            '-f',
                            '-C',
                            '+0x%x' % self.offset
                    ]
                    stdout = subprocess.check_output(cmd)
                except subprocess.CalledProcessError:
                    pass
                else:
                    self._function, line, _ = stdout.split('\n')
        assert self._function is not None
        return self._function

    def id(self):
        if self.module:
            function = self.function()
            if function != '??':
                return '%s!%s' % (self.module, self.function())
        return self.addr

    def __str__(self):
        function = self.function()
        if function != '??':
            return function
        else:
            return '%s+0x%x' % (self.module, self.offset)


class Allocation:

    def __init__(self, no, address, size, parent = None, context=None):
        self.no = no
        self.address = address
        self.size = size
        self.parent = parent
        self.context = context

    def __str__(self):
        return 'alloc #%u, address 0x%X, size = %%u' % (self.no, self.address, self.size)


class TreeNode:

    def __init__(self, label='', parent = None):
        self.label = label
        self.parent = None
        self.cost = 0
        self.children = {}

    def dump(self, indent = '', prune_ratio = 0.05):
        children = self.children.values()
        children.sort(lambda x, y: cmp(x.cost, y.cost), reverse = True)
        if len(children) == 1:
            child = children[0]
            sys.stdout.write('%s       %s\n' % (indent, child.label))
            child.dump(indent + ' ')
        else:
            for child in children:
                relative_cost = float(child.cost) / float(self.cost)
                if relative_cost < prune_ratio:
                    break
                sys.stdout.write('%s%5.2f%% %s\n' % (indent, 100.0 * relative_cost, child.label))
                child.dump(indent + '  ')

class Heap:

    def __init__(self):
        self.allocs = {}
        self.size = 0

    def add(self, alloc):
        assert alloc.no not in self.allocs
        self.allocs[alloc.no] = alloc
        self.size += alloc.size

    def pop(self, alloc_no):
        alloc = self.allocs.pop(alloc_no)
        assert self.size >= alloc.size
        self.size -= alloc.size
        return alloc
    
    def copy(self):
        heap = Heap()
        heap.allocs = self.allocs.copy()
        heap.size = self.size
        return heap

    def tree(self):
        root = TreeNode()
        for alloc in self.allocs.itervalues():
            frames = alloc.context.frames
            root.cost += alloc.size
            parent = root
            for frame in frames:
                try:
                    child = parent.children[frame]
                except KeyError:
                    child = TreeNode(frame, parent)
                    parent.children[frame] = child
                child.cost += alloc.size
                parent = child
        root.dump()

    def graph(self):
        try:
            import gprof2dot
        except ImportError:
            pass

        from gprof2dot import Profile, Function, Call, SAMPLES, SAMPLES2, TIME_RATIO, TOTAL_TIME_RATIO

        profile = Profile()
        profile[SAMPLES] = 0

        for alloc in self.allocs.itervalues():
            callchain = []

            for symbol in alloc.context.frames:
                function_id = symbol.id()
                function_name = str(symbol)
                try:
                    function = profile.functions[function_id]
                except KeyError:
                    function = Function(function_id, function_name)
                    function.module = os.path.basename(symbol.module)
                    function[SAMPLES] = 0
                    profile.add_function(function)
                callchain.append(function)

            callee = callchain[0]
            callee[SAMPLES] += alloc.size
            profile[SAMPLES] += alloc.size

            for caller in callchain[1:]:
                try:
                    call = caller.calls[callee.id]
                except KeyError:
                    call = Call(callee.id)
                    call[SAMPLES2] = alloc.size
                    caller.add_call(call)
                else:
                    call[SAMPLES2] += alloc.size

                callee = caller

        # compute derived data
        profile.validate()
        profile.find_cycles()
        profile.ratio(TIME_RATIO, SAMPLES)
        profile.call_ratios(SAMPLES2)
        profile.integrate(TOTAL_TIME_RATIO, TIME_RATIO)

        from gprof2dot import DotWriter, TEMPERATURE_COLORMAP

        writer = DotWriter(open('leakcount.dot', 'wt'))
        writer.strip = True
        writer.wrap = True
        profile.prune(0.5/100.0, 0.1/100.0)
        writer.graph(profile, TEMPERATURE_COLORMAP)


class Filter:

    def __init__(
        self,
        include_functions = (),
        exclude_functions = (),
        include_modules = (),
        exclude_modules = (),
        default=None
    ):

        self.include_function_re = self._compile(include_functions)
        self.exclude_function_re = self._compile(exclude_functions)
        self.include_module_re = self._compile(include_modules)
        self.exclude_module_re = self._compile(exclude_modules)

        includes = include_functions or include_modules
        excludes = exclude_functions or exclude_modules

        if default is None:
            if excludes:
                if includes:
                    self.default = True
                else:
                    self.default = True
            else:
                if includes:
                    self.default = False
                else:
                    self.default = True
        else:
            self.default = default
        self.default = not includes

    def _compile(self, patterns):
        if patterns:
            return re.compile('|'.join(patterns))
        else:
            return None

    def filter(self, alloc):
        context = alloc.context
        for symbol in context.frames:
            if self.include_function_re is not None or self.exclude_function_re is not None:
                function = symbol.function()
                if self.include_function_re is not None:
                    mo = self.include_function_re.search(function)
                    if mo:
                        return True
                if self.exclude_function_re is not None:
                    mo = self.exclude_function_re.search(function)
                    if mo:
                        return False
            if self.include_module_re is not None or self.exclude_module_re is not None:
                module = symbol.module
                if self.include_module_re is not None:
                    mo = self.include_module_re.search(module)
                    if mo:
                        return True
                if self.exclude_module_re is not None:
                    mo = self.exclude_module_re.search(module)
                    if mo:
                        return False
        return self.default


class Parser:

    def __init__(self, log, filter):
        self.log = GzipFile(log, 'rt')
        self.stamp = 0
        self.heap = Heap()
        self.max_heap = self.heap
        self.max_stamp = 0
        self.filter = filter

        self.modules = {}
        self.symbols = {}

    def parse(self):
        wordsize, = self.read('B')
        assert wordsize * 8 == 64
        try:
            while True:
                self.parse_event()
        except struct.error:
            pass
        except KeyboardInterrupt:
            pass
        self.on_finish()

    # ./libleakcount.so(+0xead)[0x2b54cc593ead]
    frame_re = re.compile(r'^(?P<module>[^()]*)(?:\((?P<symbol>.*)\+(?P<offset>0x[0-9a-fA-F]+)\))?\[(?P<address>0x[0-9a-fA-F]+)\]$')

    def parse_event(self):
        self.stamp += 1
        stamp = self.stamp

        addr, ssize = self.read('Pl')
        #print '0x%08x %i' % (addr, ssize)

        frames = self.parse_frames()

        if ssize > 0:
            alloc = self.create_alloc(addr, ssize, frames)
            if self.filter.filter(alloc):
                self.heap.add(alloc)
            else:
                return True
        else:
            if self.heap is self.max_heap:
                self.max_heap = self.heap.copy()
            try:
                old_alloc = self.pop_alloc(addr)
            except KeyError:
                return True

        if self.heap.size > self.max_heap.size:
            self.max_heap = self.heap
            self.max_stamp = stamp

        self.on_update(stamp)

        return True

    def parse_frames(self):
        count, = self.read('B')

        frames = []
        START, LEAKCOUNT, USER = range(3)
        layer = START
        for i in range(count):
            addr, offset, moduleNo = self.read('PPB')

            try:
                module = self.modules[moduleNo]
            except KeyError:
                length, = self.read('P')
                module = self.log.read(length)
                self.modules[moduleNo] = module

            #print Symbol(addr, module, offset)
            if layer < USER:
                if module.endswith('libleakcount.so'):
                    if layer == START:
                        layer = LEAKCOUNT
                else:
                    if layer == LEAKCOUNT:
                        layer = USER
            #print '0x%08x %r +0x%08x' % (addr, module, offset)
            if layer == USER:
                try:
                    symbol = self.symbols[addr]
                except KeyError:
                    symbol = Symbol(addr, module, offset)
                    self.symbols[addr] = symbol

                frames.append(symbol)

        assert frames
        return frames

    def create_alloc(self, address, size, frames, parent = None):
        stamp = None
        thread = None

        context = Context(stamp, thread, frames)
        alloc = Allocation(address, address, size, parent, context)
        return alloc

    def pop_alloc(self, address):
        return self.heap.pop(address)

    last_stamp = 0

    counter = 1
    interval = 1000

    def on_update(self, stamp):
        if self.counter:
            assert stamp >= self.last_stamp
            if stamp >= self.last_stamp + self.interval:
                kb = (self.heap.size + 1024 - 1)/1024
                #sys.stdout.write('%8d KB  %s\n' % (kb, '.' * (kb/16)))
                sys.stdout.write('%8d KB\n' % (kb,))
                sys.stdout.flush()
                self.last_stamp = stamp
        else:
            #print stamp, kind, alloc, address, size, thread
            #for frame in frames:
            #    print '\t', frame
            #print
            pass

    def on_finish(self):
        if self.counter:
            sys.stdout.write('\n')
        sys.stdout.write('max = %u\n' % self.max_heap.size)
        self.max_heap.tree()
        self.max_heap.graph()

    def read(self, fmt):
        size = struct.calcsize(fmt)
        data = self.log.read(size)
        return struct.unpack(fmt, data)

    def readline(self):
        line = self.log.readline()
        return self.stamp, line



def main():
    parser = optparse.OptionParser(
        usage="\n\t%prog [options] [file] ...")
    parser.add_option(
        '-g', '--graph', metavar='DOT',
        type="string", dest="graph",
        help="output dot graph]")
    parser.add_option(
        '-i', '--include-function', metavar='PATTERN',
        type="string",
        action='append',
        dest="include_functions", default=[],
        help="include functions matching the regular expression")
    parser.add_option(
        '-x', '--exclude-function', metavar='PATTERN',
        type="string",
        action='append',
        dest="exclude_functions", default=[],
        help="exclude functions matching the regular expression")
    parser.add_option(
        '--include-module', metavar='PATTERN',
        type="string",
        action='append',
        dest="include_modules", default=[],
        help="include modules matching the regular expression")
    parser.add_option(
        '--exclude-module', metavar='PATTERN',
        type="string",
        action='append',
        dest="exclude_modules", default=[],
        help="exclude modules matching the regular expression")
    # add a new option to control skew of the colorization curve
    parser.add_option(
        '--skew',
        type="float", dest="theme_skew", default=1.0,
        help="skew the colorization curve.  Values < 1.0 give more variety to lower percentages.  Value > 1.0 give less variety to lower percentages")
    (options, args) = parser.parse_args(sys.argv[1:])

    filter = Filter(
        options.include_functions,
        options.exclude_functions,
        options.include_modules,
        options.exclude_modules,
    )

    for arg in args:
        parser = Parser(arg, filter)
        parser.parse()


if __name__ == '__main__':
    main()
