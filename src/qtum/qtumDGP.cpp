#include "qtumDGP.h"

std::vector<ResultExecute> QtumDGP::callContract(const dev::Address& addrContract, std::vector<unsigned char> opcode){
    CBlock block;
    CMutableTransaction tx;
    dev::u256 gasLimit(DEFAULT_BLOCK_GASLIMIT - 1); // MAX_MONEY
    dev::Address senderAddress("ffffffffffffffffffffffffffffffffffffffff");
    tx.vout.push_back(CTxOut(0, CScript() << OP_DUP << OP_HASH160 << senderAddress.asBytes() << OP_EQUALVERIFY << OP_CHECKSIG));
    block.vtx.push_back(MakeTransactionRef(CTransaction(tx)));
 
    QtumTransaction callTransaction(0, 1, gasLimit, addrContract, opcode, dev::u256(0));
    callTransaction.forceSender(senderAddress);

    ByteCodeExec exec(block, std::vector<QtumTransaction>(1, callTransaction));
    exec.performByteCode(dev::eth::Permanence::Reverted);
    return exec.getResult();
}

dev::eth::EVMSchedule QtumDGP::getGasSchedule(unsigned int blockHeight){
    clear();
    dev::eth::EVMSchedule schedule = dev::eth::EIP158Schedule;
    if(initStorages(GasScheduleDGP, blockHeight, ParseHex("26fadbe2"))){
        schedule = createEVMSchedule();
    }
    return schedule;
}

uint32_t QtumDGP::getBlockSize(unsigned int blockHeight){
    clear();
    uint32_t blockSize = 0;
    if(initStorages(BlockSizeDGP, blockHeight, ParseHex("92ac3c62"))){
        if(!dgpevm){
            parseStorageOneUint32(blockSize);
        } else {
            parseDataOneUint32(blockSize);
        }
    }
    return blockSize;
}

uint32_t QtumDGP::getMinGasPrice(unsigned int blockHeight){
    clear();
    uint32_t minGasLimit = 1;
    if(initStorages(GasPriceDGP, blockHeight, ParseHex("3fb58819"))){
        if(!dgpevm){
            parseStorageOneUint32(minGasLimit);
        } else {
            parseDataOneUint32(minGasLimit);
        }
    }
    return minGasLimit < 1 ? 1 : minGasLimit;
}

bool QtumDGP::initStorages(const dev::Address& addr, unsigned int blockHeight, std::vector<unsigned char> data){
    initStorageDGP(addr);
    createParamsInstance();
    dev::Address address = getAddressForBlock(blockHeight);
    if(address != dev::Address()){
        if(!dgpevm){
            initStorageTemplate(address);
        } else {
            initDataTemplate(address, data);
        }
        return true;
    }
    return false;
}

void QtumDGP::initStorageDGP(const dev::Address& addr){
    storageDGP = state->storage(addr);
}

void QtumDGP::initStorageTemplate(const dev::Address& addr){
    storageTemplate = state->storage(addr);
}

void QtumDGP::initDataTemplate(const dev::Address& addr, std::vector<unsigned char>& data){
    dataTemplate = callContract(addr, data)[0].execRes.output;
}

void QtumDGP::createParamsInstance(){
    dev::h256 paramsInstanceHash = sha3(dev::h256("0000000000000000000000000000000000000000000000000000000000000000"));
    if(storageDGP.count(paramsInstanceHash)){
        dev::u256 paramsInstanceSize = storageDGP.find(paramsInstanceHash)->second.second;
        for(size_t i = 0; i < size_t(paramsInstanceSize); i++){
            std::pair<unsigned int, dev::Address> params;
            params.first = uint64_t(storageDGP.find(sha3(paramsInstanceHash))->second.second);
            ++paramsInstanceHash;
            params.second = dev::right160(dev::h256(storageDGP.find(sha3(paramsInstanceHash))->second.second));
            ++paramsInstanceHash;
            paramsInstance.push_back(params);
        }
    }
}

dev::Address QtumDGP::getAddressForBlock(unsigned int blockHeight){
    for(auto i = paramsInstance.rbegin(); i != paramsInstance.rend(); i++){
        if(i->first <= blockHeight)
            return i->second;
    }
    return dev::Address();
}

void QtumDGP::parseStorageScheduleContract(std::vector<uint32_t>& uint32Values){
    std::vector<std::pair<dev::u256, dev::u256>> data;
    for(size_t i = 0; i < 5; i++){
        dev::h256 gasScheduleHash = sha3(dev::h256(dev::u256(i)));
        if(storageTemplate.count(gasScheduleHash)){
            dev::u256 key = storageTemplate.find(gasScheduleHash)->second.first;
            dev::u256 value = storageTemplate.find(gasScheduleHash)->second.second;
            data.push_back(std::make_pair(key, value));
        }
    }

    std::sort(data.begin(), data.end(), [&data](std::pair<dev::u256, dev::u256>& a, std::pair<dev::u256, dev::u256>& b){
        return a.first < b.first;
    });

    for(std::pair<dev::u256, dev::u256> d : data){
        dev::u256 value = d.second;
        for(size_t i = 0; i < 4; i++){
            uint64_t uint64Value = uint64_t(value);
            value = value >> 64;

            uint32Values.push_back(uint32_t(uint64Value));
            uint64Value = uint64Value >> 32;
            uint32Values.push_back(uint32_t(uint64Value));
        }
    }
}

void QtumDGP::parseDataScheduleContract(std::vector<uint32_t>& uint32Values){
    size_t size = dataTemplate.size() / 32;
    for(size_t i = 0; i < size; i++){
        std::vector<unsigned char> value = std::vector<unsigned char>(dataTemplate.begin() + (i * 32), dataTemplate.begin() + ((i+1) * 32));
        dev::h256 valueTemp(value);
        uint32Values.push_back(uint64_t(dev::u256(valueTemp)));
    }
}

void QtumDGP::parseStorageOneUint32(uint32_t& value){
    dev::h256 blockSizeHash = sha3(dev::h256(dev::u256(0)));
    if(storageTemplate.count(blockSizeHash)){
        value = uint32_t(storageTemplate.find(blockSizeHash)->second.second);
    }
}

void QtumDGP::parseDataOneUint32(uint32_t& value){
    if(dataTemplate.size() == 32){
        value = uint64_t(dev::u256(dev::h256(dataTemplate)));
    }
}

dev::eth::EVMSchedule QtumDGP::createEVMSchedule(){
    dev::eth::EVMSchedule schedule = dev::eth::EIP158Schedule;
    std::vector<uint32_t> uint32Values;

    if(!dgpevm){
        parseStorageScheduleContract(uint32Values);
    } else {
        parseDataScheduleContract(uint32Values);
    }

    if(uint32Values.size() >= 39){
        schedule.tierStepGas = {{uint32Values[0], uint32Values[1], uint32Values[2], uint32Values[3],
                                uint32Values[4], uint32Values[5], uint32Values[6], uint32Values[7]}};
        schedule.expGas = uint32Values[8];
        schedule.expByteGas = uint32Values[9];
        schedule.sha3Gas = uint32Values[10];
        schedule.sha3WordGas = uint32Values[11];
        schedule.sloadGas = uint32Values[12];
        schedule.sstoreSetGas = uint32Values[13];
        schedule.sstoreResetGas = uint32Values[14];
        schedule.sstoreRefundGas = uint32Values[15];
        schedule.jumpdestGas = uint32Values[16];
        schedule.logGas = uint32Values[17];
        schedule.logDataGas = uint32Values[18];
        schedule.logTopicGas = uint32Values[19];
        schedule.createGas = uint32Values[20];
        schedule.callGas = uint32Values[21];
        schedule.callStipend = uint32Values[22];
        schedule.callValueTransferGas = uint32Values[23];
        schedule.callNewAccountGas = uint32Values[24];
        schedule.suicideRefundGas = uint32Values[25];
        schedule.memoryGas = uint32Values[26];
        schedule.quadCoeffDiv = uint32Values[27];
        schedule.createDataGas = uint32Values[28];
        schedule.txGas = uint32Values[29];
        schedule.txCreateGas = uint32Values[30];
        schedule.txDataZeroGas = uint32Values[31];
        schedule.txDataNonZeroGas = uint32Values[32];
        schedule.copyGas = uint32Values[33];
        schedule.extcodesizeGas = uint32Values[34];
        schedule.extcodecopyGas = uint32Values[35];
        schedule.balanceGas = uint32Values[36];
        schedule.suicideGas = uint32Values[37];
        schedule.maxCodeSize = uint32Values[38];
    }
    return schedule;
}

void QtumDGP::clear(){
    templateContract = dev::Address();
    storageDGP.clear();
    storageTemplate.clear();
    paramsInstance.clear();
}
