#!python -u

import os, sys
import datetime
import re
import glob
import tarfile
import subprocess
import shutil
import time

def next_build_number():
    try:
        file = open('.build_number', 'r')
        build_number = file.read()
        file.close()
    except IOError:
        build_number = '0'

    file = open('.build_number', 'w')
    file.write(str(int(build_number) + 1))
    file.close()

    return build_number


def make_header():
    now = datetime.datetime.now()

    file = open('include\\version.h', 'w')

    file.write('#define VENDOR_NAME_STR\t\t"' + os.environ['VENDOR_NAME'] + '"\n')
    file.write('#define VENDOR_PREFIX_STR\t"' + os.environ['VENDOR_PREFIX'] + '"\n')

    if 'VENDOR_DEVICE_ID' in os.environ.keys():
        file.write('#define VENDOR_DEVICE_ID_STR\t"' + os.environ['VENDOR_DEVICE_ID'] + '"\n')

    file.write('#define PRODUCT_NAME_STR\t"' + os.environ['PRODUCT_NAME'] + '"\n')
    file.write('\n')

    file.write('#define MAJOR_VERSION\t\t' + os.environ['MAJOR_VERSION'] + '\n')
    file.write('#define MAJOR_VERSION_STR\t"' + os.environ['MAJOR_VERSION'] + '"\n')
    file.write('\n')

    file.write('#define MINOR_VERSION\t\t' + os.environ['MINOR_VERSION'] + '\n')
    file.write('#define MINOR_VERSION_STR\t"' + os.environ['MINOR_VERSION'] + '"\n')
    file.write('\n')

    file.write('#define MICRO_VERSION\t\t' + os.environ['MICRO_VERSION'] + '\n')
    file.write('#define MICRO_VERSION_STR\t"' + os.environ['MICRO_VERSION'] + '"\n')
    file.write('\n')

    file.write('#define BUILD_NUMBER\t\t' + os.environ['BUILD_NUMBER'] + '\n')
    file.write('#define BUILD_NUMBER_STR\t"' + os.environ['BUILD_NUMBER'] + '"\n')
    file.write('\n')

    file.write('#define YEAR\t\t\t' + str(now.year) + '\n')
    file.write('#define YEAR_STR\t\t"' + str(now.year) + '"\n')
    file.write('\n')

    file.write('#define MONTH\t\t\t' + str(now.month) + '\n')
    file.write('#define MONTH_STR\t\t"' + str(now.month) + '"\n')
    file.write('\n')

    file.write('#define DAY\t\t\t' + str(now.day) + '\n')
    file.write('#define DAY_STR\t\t\t"' + str(now.day) + '"\n')
    file.write('\n')

    file.close()


def copy_inf(vs, name):
    src = open('src\\%s.inf' % name, 'r')
    dst = open('%s\\%s.inf' % (vs, name), 'w')

    for line in src:
        line = re.sub('@MAJOR_VERSION@', os.environ['MAJOR_VERSION'], line)
        line = re.sub('@MINOR_VERSION@', os.environ['MINOR_VERSION'], line)
        line = re.sub('@MICRO_VERSION@', os.environ['MICRO_VERSION'], line)
        line = re.sub('@BUILD_NUMBER@', os.environ['BUILD_NUMBER'], line)
        line = re.sub('@VENDOR_NAME@', os.environ['VENDOR_NAME'], line)
        line = re.sub('@PRODUCT_NAME@', os.environ['PRODUCT_NAME'], line)

        if re.search('@VENDOR_DEVICE_ID@', line):
            if 'VENDOR_DEVICE_ID' not in os.environ.keys():
                continue
            line = re.sub('@VENDOR_DEVICE_ID@', os.environ['VENDOR_DEVICE_ID'], line)

        dst.write(line)

    dst.close()
    src.close()


def get_expired_symbols(name, age = 30):
    path = os.path.join(os.environ['SYMBOL_SERVER'], '000Admin\\history.txt')

    try:
        file = open(path, 'r')
    except IOError:
        return []

    threshold = datetime.datetime.utcnow() - datetime.timedelta(days = age)

    expired = []

    for line in file:
        item = line.split(',')

        if (re.match('add', item[1])):
            id = item[0]
            date = item[3].split('/')
            time = item[4].split(':')
            tag = item[5].strip('"')

            age = datetime.datetime(year = int(date[2]),
                                    month = int(date[0]),
                                    day = int(date[1]),
                                    hour = int(time[0]),
                                    minute = int(time[1]),
                                    second = int(time[2]))
            if (tag == name and age < threshold):
                expired.append(id)

        elif (re.match('del', item[1])):
            id = item[2].rstrip()
            try:
                expired.remove(id)
            except ValueError:
                pass

    file.close()

    return expired


def get_configuration(release, debug):
    configuration = release

    if debug:
        configuration += ' Debug'
    else:
        configuration += ' Release'

    return configuration


def get_target_path(release, arch, debug, vs):
    configuration = get_configuration(release, debug)
    name = ''.join(configuration.split(' '))
    target = { 'x86': os.sep.join([name, 'Win32']), 'x64': os.sep.join([name, 'x64']) }
    target_path = os.sep.join([vs, target[arch]])

    return target_path


def shell(command, dir):
    print(dir)
    print(command)
    sys.stdout.flush()
    
    sub = subprocess.Popen(' '.join(command), cwd=dir,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)

    for line in sub.stdout:
        print(line.decode(sys.getdefaultencoding()).rstrip())

    sub.wait()

    return sub.returncode


class msbuild_failure(Exception):
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

def msbuild(platform, configuration, target, file, args, dir):
    os.environ['PLATFORM'] = platform
    os.environ['CONFIGURATION'] = configuration
    os.environ['TARGET'] = target
    os.environ['FILE'] = file
    os.environ['EXTRA'] = args

    bin = os.path.join(os.getcwd(), 'msbuild.bat')

    status = shell([bin], dir)

    if (status != 0):
        raise msbuild_failure(configuration)


def build_sln(name, release, arch, debug, vs):
    configuration = get_configuration(release, debug)

    if arch == 'x86':
        platform = 'Win32'
    elif arch == 'x64':
        platform = 'x64'

    cwd = os.getcwd()

    msbuild(platform, configuration, 'Build', name + '.sln', '', vs)


def remove_timestamps(path):
    try:
        os.unlink(path + '.orig')
    except OSError:
        pass

    os.rename(path, path + '.orig')

    src = open(path + '.orig', 'r')
    dst = open(path, 'w')

    for line in src:
        if line.find('TimeStamp') == -1:
            dst.write(line)

    dst.close()
    src.close()

def sdv_clean(name, vs):
    path = [vs, name, 'sdv']
    print(path)

    shutil.rmtree(os.path.join(*path), True)

    path = [vs, name, 'sdv.temp']
    print(path)

    shutil.rmtree(os.path.join(*path), True)

    path = [vs, name, 'staticdv.job']
    print(path)

    try:
        os.unlink(os.path.join(*path))
    except OSError:
        pass

    path = [vs, name, 'refine.sdv']
    print(path)

    try:
        os.unlink(os.path.join(*path))
    except OSError:
        pass

    path = [vs, name, 'sdv-map.h']
    print(path)

    try:
        os.unlink(os.path.join(*path))
    except OSError:
        pass


def run_sdv(name, dir, vs):
    configuration = get_configuration('Windows 8', False)
    platform = 'x64'

    msbuild(platform, configuration, 'Build', name + '.vcxproj',
            '', os.path.join(vs, name))

    sdv_clean(name, vs)

    msbuild(platform, configuration, 'sdv', name + '.vcxproj',
            '/p:Inputs="/scan"', os.path.join(vs, name))

    path = [vs, name, 'sdv-map.h']
    file = open(os.path.join(*path), 'r')

    for line in file:
        print(line)

    file.close()

    msbuild(platform, configuration, 'sdv', name + '.vcxproj',
            '/p:Inputs="/check:default.sdv"', os.path.join(vs, name))

    path = [vs, name, 'sdv', 'SDV.DVL.xml']
    remove_timestamps(os.path.join(*path))

    msbuild(platform, configuration, 'dvl', name + '.vcxproj',
            '', os.path.join(vs, name))

    path = [vs, name, name + '.DVL.XML']
    shutil.copy(os.path.join(*path), dir)

    path = [vs, name, 'refine.sdv']
    if os.path.isfile(os.path.join(*path)):
        msbuild(platform, configuration, 'sdv', name + '.vcxproj',
                '/p:Inputs=/refine', os.path.join(vs, name))


def symstore_del(name, age):
    symstore_path = [os.environ['KIT'], 'Debuggers']
    if os.environ['PROCESSOR_ARCHITECTURE'] == 'x86':
        symstore_path.append('x86')
    else:
        symstore_path.append('x64')
    symstore_path.append('symstore.exe')

    symstore = os.path.join(*symstore_path)

    for id in get_expired_symbols(name, age):
        command=['"' + symstore + '"']
        command.append('del')
        command.append('/i')
        command.append(str(id))
        command.append('/s')
        command.append(os.environ['SYMBOL_SERVER'])

        shell(command, None)


def symstore_add(name, release, arch, debug, vs):
    target_path = get_target_path(release, arch, debug, vs)

    symstore_path = [os.environ['KIT'], 'Debuggers']
    if os.environ['PROCESSOR_ARCHITECTURE'] == 'x86':
        symstore_path.append('x86')
    else:
        symstore_path.append('x64')
    symstore_path.append('symstore.exe')

    symstore = os.path.join(*symstore_path)

    version = '.'.join([os.environ['MAJOR_VERSION'],
                        os.environ['MINOR_VERSION'],
                        os.environ['MICRO_VERSION'],
                        os.environ['BUILD_NUMBER']])

    command=['"' + symstore + '"']
    command.append('add')
    command.append('/s')
    command.append(os.environ['SYMBOL_SERVER'])
    command.append('/r')
    command.append('/f')
    command.append('*.pdb')
    command.append('/t')
    command.append(name)
    command.append('/v')
    command.append(version)

    shell(command, target_path)


def manifest():
    cmd = ['git', 'ls-tree', '-r', '--name-only', 'HEAD']

    sub = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    output = sub.communicate()[0]
    ret = sub.returncode

    if ret != 0:
        raise(Exception("Error %d in : %s" % (ret, cmd)))

    return output.decode('utf-8')


def archive(filename, files, tgz=False):
    print(filename)
    access='w'
    if tgz:
        access='w:gz'
    tar = tarfile.open(filename, access)
    for name in files :
        try:
            tar.add(name)
        except:
            pass
    tar.close()



def getVsVersion():
    vsenv ={} 
    vars = subprocess.check_output([os.environ['VS']+'\\VC\\vcvarsall.bat', '&&', 'set'], shell=True)
    for var in vars.splitlines():
        k, _, v = map(str.strip, var.strip().decode('utf-8').partition('='))
        if k.startswith('?'):
            continue
        vsenv[k] = v

    if vsenv['VisualStudioVersion'] == '11.0' :
        return 'vs2012'
    elif vsenv['VisualStudioVersion'] == '12.0' :
        return 'vs2013'

if __name__ == '__main__':
    debug = { 'checked': True, 'free': False }
    sdv = { 'nosdv': False, None: True }
    driver = 'xenbus'
    vs = getVsVersion()

    if 'VENDOR_NAME' not in os.environ.keys():
        os.environ['VENDOR_NAME'] = 'Xen Project'

    if 'VENDOR_PREFIX' not in os.environ.keys():
        os.environ['VENDOR_PREFIX'] = 'XP'

    if 'PRODUCT_NAME' not in os.environ.keys():
        os.environ['PRODUCT_NAME'] = 'Xen'

    os.environ['MAJOR_VERSION'] = '8'
    os.environ['MINOR_VERSION'] = '2'
    os.environ['MICRO_VERSION'] = '0'

    if 'BUILD_NUMBER' not in os.environ.keys():
        os.environ['BUILD_NUMBER'] = next_build_number()

    if 'GIT_REVISION' in os.environ.keys():
        revision = open('revision', 'w')
        print(os.environ['GIT_REVISION'], file=revision)
        revision.close()

    print("VENDOR_NAME\t\t'%s'" % os.environ['VENDOR_NAME'])
    print("VENDOR_PREFIX\t\t'%s'" % os.environ['VENDOR_PREFIX'])

    if 'VENDOR_DEVICE_ID' in os.environ.keys():
        print("VENDOR_DEVICE_ID\t'%s'" % os.environ['VENDOR_DEVICE_ID'])

    print("PRODUCT_NAME\t\t'%s'" % os.environ['PRODUCT_NAME'])
    print("MAJOR_VERSION\t\t%s" % os.environ['MAJOR_VERSION'])
    print("MINOR_VERSION\t\t%s" % os.environ['MINOR_VERSION'])
    print("MICRO_VERSION\t\t%s" % os.environ['MICRO_VERSION'])
    print("BUILD_NUMBER\t\t%s" % os.environ['BUILD_NUMBER'])
    print()

    make_header()
    copy_inf(vs, driver)

    symstore_del(driver, 30)

    if vs=='vs2012':
        release = 'Windows Vista'
    else:
        release = 'Windows 7'

    build_sln(driver, release, 'x86', debug[sys.argv[1]], vs)
    build_sln(driver, release, 'x64', debug[sys.argv[1]], vs)

    symstore_add(driver, release, 'x86', debug[sys.argv[1]], vs)
    symstore_add(driver, release, 'x64', debug[sys.argv[1]], vs)

    if len(sys.argv) <= 2 or sdv[sys.argv[2]]:
        run_sdv('xen', driver, vs)
        run_sdv('xenfilt', driver, vs)
        run_sdv('xenbus', driver, vs)

    archive(driver + '\\source.tgz', manifest().splitlines(), tgz=True)
    archive(driver + '.tar', [driver,'revision'])


